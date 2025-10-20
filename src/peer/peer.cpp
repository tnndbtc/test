// ============= peer.cpp =============
/**
 * @file peer.cpp
 * @brief Implementation of P2P network connection management
 *
 * Implements TCP-based peer-to-peer networking with thread-per-connection
 * model, socket configuration (keep-alive, non-blocking), and connection
 * lifecycle management. Handles both inbound and outbound connections.
 */

#include "peer/peer.h"
#include "logger/logger.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <algorithm>

// ============= CPeerConnection Implementation =============

/**
 * @brief Default constructor - creates disconnected peer
 *
 * Initializes all fields to default values (no socket, no connection).
 */
CPeerConnection::CPeerConnection()
    : n_socket(-1), str_address(""), n_port(0), f_connected(false), f_active(false) {}

/**
 * @brief Construct peer with address and port
 * @param str_addr Peer IP address or hostname
 * @param n_port_num Peer listening port
 *
 * Creates peer connection object but does not establish connection.
 * Call ConnectToPeer() to actually connect.
 */
CPeerConnection::CPeerConnection(const std::string& str_addr, int n_port_num)
    : n_socket(-1), str_address(str_addr), n_port(n_port_num), f_connected(false), f_active(false) {}

/**
 * @brief Destructor - closes connection and joins thread
 *
 * Performs cleanup in correct order:
 * 1. Signal thread to stop (f_active = false)
 * 2. Wait for thread to finish (join)
 * 3. Close socket and mark invalid
 *
 * This prevents race conditions during shutdown.
 */
CPeerConnection::~CPeerConnection() {
    f_active = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    if (n_socket >= 0) {
        close(n_socket);
        n_socket = -1;
    }
}

/**
 * @brief Move constructor for transferring ownership
 * @param other Peer connection to move from
 *
 * Transfers all resources (socket, thread, address) from other.
 * Resets other to disconnected state to prevent double-close.
 * Uses atomic load() to safely copy f_active flag.
 */
CPeerConnection::CPeerConnection(CPeerConnection&& other) noexcept
    : n_socket(other.n_socket),
      str_address(std::move(other.str_address)),
      n_port(other.n_port),
      f_connected(other.f_connected),
      f_active(other.f_active.load()),
      m_thread(std::move(other.m_thread)) {
    other.n_socket = -1;
    other.f_connected = false;
    other.f_active = false;
}

/**
 * @brief Move assignment for transferring ownership
 * @param other Peer connection to move from
 * @return Reference to this
 *
 * Safely transfers resources by:
 * 1. Cleaning up existing resources (close socket, join thread)
 * 2. Moving resources from other
 * 3. Resetting other to prevent double-free
 *
 * Self-assignment check prevents resource destruction when this == &other.
 */
CPeerConnection& CPeerConnection::operator=(CPeerConnection&& other) noexcept {
    if (this != &other) {
        // Clean up existing resources
        if (n_socket >= 0) {
            close(n_socket);
        }
        if (m_thread.joinable()) {
            m_thread.join();
        }

        // Move from other
        n_socket = other.n_socket;
        str_address = std::move(other.str_address);
        n_port = other.n_port;
        f_connected = other.f_connected;
        f_active = other.f_active.load();
        m_thread = std::move(other.m_thread);

        // Reset other
        other.n_socket = -1;
        other.f_connected = false;
        other.f_active = false;
    }
    return *this;
}

// ============= CPeerManager Implementation =============

/**
 * @brief Construct peer manager with listening port
 * @param n_port Port to listen on for inbound connections
 *
 * Initializes peer manager in stopped state. Reserves space for
 * MAX_OUTBOUND_PEERS to avoid vector reallocations during operation.
 * Call Start() to begin accepting connections.
 */
CPeerManager::CPeerManager(int n_port)
    : n_listen_port(n_port), n_listen_socket(-1), f_running(false), f_stop_requested(false) {
    m_outbound_peers.reserve(MAX_OUTBOUND_PEERS);
}

/**
 * @brief Destructor - stops networking and cleans up resources
 *
 * Calls Stop() to ensure all threads are joined and sockets closed.
 * Safe to call even if peer manager was never started.
 */
CPeerManager::~CPeerManager() {
    Stop();
}

/**
 * @brief Start peer manager and networking threads
 * @return true if started successfully, false on error
 *
 * Startup sequence:
 * 1. Check if already running (idempotent)
 * 2. Create and bind listening socket
 * 3. Set running flags
 * 4. Start listener thread (accepts inbound connections)
 * 5. Start peer management thread (cleanup and maintenance)
 *
 * Returns false if socket creation fails. Already-running state
 * returns true without error.
 */
bool CPeerManager::Start() {
    if (f_running) {
        LOG_WARN("Peer manager already running");
        return true;
    }

    // Create listen socket for inbound connections
    if (!CreateListenSocket()) {
        LOG_ERROR("Failed to create listen socket for peer manager");
        return false;
    }

    f_running = true;
    f_stop_requested = false;

    // Start listener thread for inbound connections
    m_listener_thread = std::thread(&CPeerManager::ListenerThread, this);

    // Start peer management thread
    m_peer_thread = std::thread(&CPeerManager::PeerThread, this);

    LOG_TRACE("Peer Manager started on port " + std::to_string(n_listen_port));
    LOG_TRACE("Maximum outbound peers: " + std::to_string(MAX_OUTBOUND_PEERS));

    return true;
}

/**
 * @brief Stop peer manager and all networking
 *
 * Shutdown sequence (thread-safe and idempotent):
 * 1. Check if running (safe to call when stopped)
 * 2. Set stop flags and close listening socket
 * 3. Signal all peer connections to stop (f_active = false)
 * 4. Shutdown and close all peer sockets
 * 5. Join listener and peer management threads
 * 6. Join all peer connection threads
 * 7. Clear peer list
 *
 * Blocks until all threads have terminated. Uses mutex to safely
 * access peer list during shutdown.
 */
void CPeerManager::Stop() {
    if (!f_running) {
        return;
    }

    LOG_INFO("Stopping peer manager");
    f_stop_requested = true;
    f_running = false;

    // Close listen socket
    CloseListenSocket();

    // Stop all peer connections
    {
        std::lock_guard<std::mutex> lock(cs_peers);
        for (auto& p_peer : m_outbound_peers) {
            if (p_peer) {
                p_peer->f_active = false;
                if (p_peer->n_socket >= 0) {
                    shutdown(p_peer->n_socket, SHUT_RDWR);
                    close(p_peer->n_socket);
                    p_peer->n_socket = -1;
                }
            }
        }
    }

    // Join threads
    if (m_listener_thread.joinable()) {
        m_listener_thread.join();
    }

    if (m_peer_thread.joinable()) {
        m_peer_thread.join();
    }

    // Wait for all peer connection threads to finish
    {
        std::lock_guard<std::mutex> lock(cs_peers);
        for (auto& p_peer : m_outbound_peers) {
            if (p_peer && p_peer->m_thread.joinable()) {
                p_peer->m_thread.join();
            }
        }
        m_outbound_peers.clear();
    }

    LOG_INFO("Peer manager stopped");
}

/**
 * @brief Check if peer manager is running
 * @return true if running, false otherwise
 *
 * Thread-safe read of atomic flag. Returns true between successful
 * Start() and Stop() completion.
 */
bool CPeerManager::IsRunning() const {
    return f_running;
}

/**
 * @brief Create and bind listening socket
 * @return true if socket created and bound successfully, false on error
 *
 * Socket creation sequence:
 * 1. Create IPv4 TCP socket
 * 2. Set SO_REUSEADDR to allow rapid restart after shutdown
 * 3. Bind to INADDR_ANY (all network interfaces) on configured port
 * 4. Start listening with backlog of 10 pending connections
 *
 * On any error, cleans up socket and returns false.
 */
bool CPeerManager::CreateListenSocket() {
    // Create socket
    n_listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (n_listen_socket < 0) {
        std::cerr << "[Peer Manager] Failed to create listen socket\n";
        return false;
    }

    // Set socket options
    int n_opt = 1;
    setsockopt(n_listen_socket, SOL_SOCKET, SO_REUSEADDR, &n_opt, sizeof(n_opt));

    // Bind socket
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(n_listen_port);

    if (bind(n_listen_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[Peer Manager] Failed to bind to port " << n_listen_port << "\n";
        close(n_listen_socket);
        n_listen_socket = -1;
        return false;
    }

    // Listen
    if (listen(n_listen_socket, 10) < 0) {
        std::cerr << "[Peer Manager] Failed to listen\n";
        close(n_listen_socket);
        n_listen_socket = -1;
        return false;
    }

    return true;
}

/**
 * @brief Close listening socket
 *
 * Shuts down server socket gracefully (SHUT_RDWR), causing any
 * blocking accept() calls to fail and listener thread to exit.
 * Marks socket as invalid (-1) after closing.
 */
void CPeerManager::CloseListenSocket() {
    if (n_listen_socket >= 0) {
        shutdown(n_listen_socket, SHUT_RDWR);
        close(n_listen_socket);
        n_listen_socket = -1;
    }
}

/**
 * @brief Enable TCP keep-alive on socket
 * @param n_socket Socket file descriptor
 * @return true if keep-alive enabled successfully, false on error
 *
 * Configures socket to send periodic keep-alive probes to detect
 * dead connections. Settings:
 * - 60 seconds idle before first probe
 * - 10 seconds between probes
 * - 6 failed probes before closing connection
 *
 * Platform differences:
 * - macOS: Uses TCP_KEEPALIVE option
 * - Linux: Uses TCP_KEEPIDLE option
 *
 * Returns false only if SO_KEEPALIVE fails; other options failing
 * generate warnings but don't prevent operation.
 */
bool CPeerManager::SetSocketKeepAlive(int n_socket) {
    int n_keepalive = 1;
    int n_keepidle = 60;      // Start sending keepalive probes after 60 seconds
    int n_keepintvl = 10;     // Send keepalive probes every 10 seconds
    int n_keepcnt = 6;        // Close connection after 6 failed probes

    if (setsockopt(n_socket, SOL_SOCKET, SO_KEEPALIVE, &n_keepalive, sizeof(n_keepalive)) < 0) {
        LOG_ERROR("Failed to set SO_KEEPALIVE");
        return false;
    }

#ifdef __APPLE__
    // macOS uses TCP_KEEPALIVE instead of TCP_KEEPIDLE
    if (setsockopt(n_socket, IPPROTO_TCP, TCP_KEEPALIVE, &n_keepidle, sizeof(n_keepidle)) < 0) {
        LOG_WARN("Failed to set TCP_KEEPALIVE");
    }
#else
    // Linux uses TCP_KEEPIDLE
    if (setsockopt(n_socket, IPPROTO_TCP, TCP_KEEPIDLE, &n_keepidle, sizeof(n_keepidle)) < 0) {
        LOG_WARN("Failed to set TCP_KEEPIDLE");
    }
#endif

    if (setsockopt(n_socket, IPPROTO_TCP, TCP_KEEPINTVL, &n_keepintvl, sizeof(n_keepintvl)) < 0) {
        LOG_WARN("Failed to set TCP_KEEPINTVL");
    }

    if (setsockopt(n_socket, IPPROTO_TCP, TCP_KEEPCNT, &n_keepcnt, sizeof(n_keepcnt)) < 0) {
        LOG_WARN("Failed to set TCP_KEEPCNT");
    }

    return true;
}

/**
 * @brief Set socket blocking/non-blocking mode
 * @param n_socket Socket file descriptor
 * @param f_non_blocking true for non-blocking, false for blocking
 * @return true if mode set successfully, false on error
 *
 * Uses fcntl() to modify O_NONBLOCK flag:
 * - Non-blocking: Operations return immediately with EAGAIN/EWOULDBLOCK
 * - Blocking: Operations wait until data available or error
 *
 * Returns false if fcntl() fails to get or set flags.
 */
bool CPeerManager::SetSocketNonBlocking(int n_socket, bool f_non_blocking) {
    int n_flags = fcntl(n_socket, F_GETFL, 0);
    if (n_flags < 0) {
        return false;
    }

    if (f_non_blocking) {
        n_flags |= O_NONBLOCK;
    } else {
        n_flags &= ~O_NONBLOCK;
    }

    return fcntl(n_socket, F_SETFL, n_flags) >= 0;
}

/**
 * @brief Main peer management thread function
 *
 * Runs periodic maintenance tasks:
 * - Cleans up disconnected peers from peer list
 * - Runs every 5 seconds while peer manager is active
 *
 * Exits when f_stop_requested is set by Stop().
 */
void CPeerManager::PeerThread() {
    LOG_INFO("Peer management thread started");

    while (!f_stop_requested) {
        // Clean up disconnected peers
        CleanupDisconnectedPeers();

        // Sleep for a bit before next iteration
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    LOG_TRACE("Peer management thread stopped");
}

/**
 * @brief Listener thread function for accepting connections
 *
 * Continuously accepts incoming TCP connections until stopped.
 * For each accepted connection:
 * 1. Extracts peer IP address and port
 * 2. Enables TCP keep-alive
 * 3. Logs connection (currently closes immediately - TODO)
 *
 * Ignores EAGAIN/EWOULDBLOCK errors (non-blocking socket behavior).
 * Exits when f_stop_requested is set and listening socket is closed.
 */
void CPeerManager::ListenerThread() {
    LOG_INFO("Peer listener thread started");

    while (!f_stop_requested) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int n_client_socket = accept(n_listen_socket, (sockaddr*)&client_addr, &client_len);

        if (n_client_socket < 0) {
            if (!f_stop_requested) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    LOG_ERROR("Peer listener accept() failed: " + std::string(strerror(errno)));
                }
            }
            continue;
        }

        // Get peer address
        char str_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, str_ip, INET_ADDRSTRLEN);
        int n_peer_port = ntohs(client_addr.sin_port);

        LOG_INFO("Inbound peer connection from " + std::string(str_ip) + ":" + std::to_string(n_peer_port));

        // Set socket keepalive
        SetSocketKeepAlive(n_client_socket);

        // TODO: Handle inbound peer connection
        // For now, just close it since we're focusing on outbound connections
        close(n_client_socket);
    }

    LOG_INFO("Peer listener thread stopped");
}

/**
 * @brief Connection thread function for individual peer
 * @param p_peer Pointer to peer connection to handle
 *
 * Manages communication with a single peer in dedicated thread.
 * Runs keep-alive loop that:
 * 1. Checks if socket is still valid
 * 2. Handles messages (TODO: not yet implemented)
 * 3. Sleeps 1 second between iterations
 *
 * Exits when peer becomes inactive (f_active = false) or
 * peer manager requests shutdown (f_stop_requested).
 * NULL peer pointer causes immediate return.
 */
void CPeerManager::ConnectionThread(CPeerConnection* p_peer) {
    if (!p_peer) {
        return;
    }

    LOG_INFO("Connection thread started for peer " + p_peer->str_address + ":" + std::to_string(p_peer->n_port));

    // Connection keep-alive loop
    while (p_peer->f_active && !f_stop_requested) {
        // Check if socket is still connected
        if (p_peer->n_socket < 0) {
            LOG_WARN("Peer socket closed: " + p_peer->str_address);
            break;
        }

        // TODO: Implement message handling
        // For now, just keep the connection alive
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    LOG_INFO("Connection thread stopped for peer " + p_peer->str_address);
}

/**
 * @brief Initiate outbound connection to peer
 * @param str_address Peer IP address or hostname
 * @param n_port Peer listening port
 * @return true if connection established, false on error
 *
 * Connection sequence:
 * 1. Create TCP socket
 * 2. Enable TCP keep-alive
 * 3. Convert address string to binary form (inet_pton)
 * 4. Attempt connection (blocking)
 * 5. Create CPeerConnection object with socket
 * 6. Start connection thread for message handling
 * 7. Add to outbound peers list (mutex-protected)
 *
 * On any error, closes socket and returns false.
 * This is a private method called by AddPeer() after validation.
 */
bool CPeerManager::ConnectToPeer(const std::string& str_address, int n_port) {
    // Create socket
    int n_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (n_socket < 0) {
        LOG_ERROR("Failed to create socket for peer " + str_address);
        return false;
    }

    // Set socket keepalive
    if (!SetSocketKeepAlive(n_socket)) {
        LOG_WARN("Failed to set keepalive for peer " + str_address);
    }

    // Connect to peer
    sockaddr_in peer_addr{};
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(n_port);

    if (inet_pton(AF_INET, str_address.c_str(), &peer_addr.sin_addr) <= 0) {
        LOG_ERROR("Invalid peer address: " + str_address);
        close(n_socket);
        return false;
    }

    if (connect(n_socket, (sockaddr*)&peer_addr, sizeof(peer_addr)) < 0) {
        LOG_ERROR("Failed to connect to peer " + str_address + ":" + std::to_string(n_port) + " - " + strerror(errno));
        close(n_socket);
        return false;
    }

    // Create peer connection object
    auto p_peer = std::make_unique<CPeerConnection>(str_address, n_port);
    p_peer->n_socket = n_socket;
    p_peer->f_connected = true;
    p_peer->f_active = true;

    // Start connection thread
    p_peer->m_thread = std::thread(&CPeerManager::ConnectionThread, this, p_peer.get());

    // Add to outbound peers list
    {
        std::lock_guard<std::mutex> lock(cs_peers);
        m_outbound_peers.push_back(std::move(p_peer));
    }

    LOG_INFO("Successfully connected to peer " + str_address + ":" + std::to_string(n_port));

    return true;
}

/**
 * @brief Disconnect from peer and cleanup resources
 * @param p_peer Pointer to peer connection to disconnect
 *
 * Disconnection sequence:
 * 1. Mark peer as inactive (stops connection thread loop)
 * 2. Mark as disconnected
 * 3. Shutdown socket (SHUT_RDWR)
 * 4. Close socket and mark invalid
 *
 * Safe to call on NULL pointer (no-op).
 * Does not remove from peer list - use CleanupDisconnectedPeers().
 */
void CPeerManager::DisconnectPeer(CPeerConnection* p_peer) {
    if (!p_peer) {
        return;
    }

    p_peer->f_active = false;
    p_peer->f_connected = false;

    if (p_peer->n_socket >= 0) {
        shutdown(p_peer->n_socket, SHUT_RDWR);
        close(p_peer->n_socket);
        p_peer->n_socket = -1;
    }

    LOG_INFO("Disconnected peer " + p_peer->str_address);
}

/**
 * @brief Remove disconnected peers from peer list
 *
 * Thread-safe cleanup using erase-remove idiom:
 * 1. Acquires mutex lock
 * 2. Finds all peers where f_connected == false
 * 3. Removes them from m_outbound_peers vector
 *
 * Called periodically by PeerThread() every 5 seconds.
 * Assumes peer connection threads have already been joined.
 */
void CPeerManager::CleanupDisconnectedPeers() {
    std::lock_guard<std::mutex> lock(cs_peers);

    // Remove disconnected peers
    m_outbound_peers.erase(
        std::remove_if(m_outbound_peers.begin(), m_outbound_peers.end(),
            [](const std::unique_ptr<CPeerConnection>& p_peer) {
                return p_peer && !p_peer->f_connected;
            }),
        m_outbound_peers.end()
    );
}

/**
 * @brief Add outbound connection to peer
 * @param str_address Peer IP address or hostname
 * @param n_port Peer listening port
 * @return true if connection initiated successfully, false on error
 *
 * Validation checks (thread-safe with mutex):
 * 1. Verify we haven't reached MAX_OUTBOUND_PEERS limit
 * 2. Check if already connected to this address:port
 *
 * If validation passes, delegates to ConnectToPeer() for actual
 * connection. Connection happens synchronously but message handling
 * occurs asynchronously in background thread.
 */
bool CPeerManager::AddPeer(const std::string& str_address, int n_port) {
    // Check if we've reached max outbound peers
    {
        std::lock_guard<std::mutex> lock(cs_peers);
        if (m_outbound_peers.size() >= MAX_OUTBOUND_PEERS) {
            LOG_WARN("Maximum outbound peers reached (" + std::to_string(MAX_OUTBOUND_PEERS) + ")");
            return false;
        }

        // Check if already connected to this peer
        for (const auto& p_peer : m_outbound_peers) {
            if (p_peer && p_peer->str_address == str_address && p_peer->n_port == n_port) {
                LOG_WARN("Already connected to peer " + str_address + ":" + std::to_string(n_port));
                return false;
            }
        }
    }

    return ConnectToPeer(str_address, n_port);
}

/**
 * @brief Get count of active outbound peer connections
 * @return Number of outbound peers
 *
 * Thread-safe count of peers in outbound peer list.
 * Includes both connected and disconnected peers that haven't
 * been cleaned up yet by CleanupDisconnectedPeers().
 */
size_t CPeerManager::GetOutboundPeerCount() const {
    std::lock_guard<std::mutex> lock(cs_peers);
    return m_outbound_peers.size();
}

/**
 * @brief Get list of connected peer addresses
 * @return Vector of "address:port" strings for connected peers
 *
 * Thread-safe snapshot of currently connected peers.
 * Filters out disconnected peers (f_connected == false).
 * Returns empty vector if no peers connected.
 */
std::vector<std::string> CPeerManager::GetConnectedPeers() const {
    std::vector<std::string> peers;
    std::lock_guard<std::mutex> lock(cs_peers);

    for (const auto& p_peer : m_outbound_peers) {
        if (p_peer && p_peer->f_connected) {
            peers.push_back(p_peer->str_address + ":" + std::to_string(p_peer->n_port));
        }
    }

    return peers;
}
