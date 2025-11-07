// ============= peer_manager.cpp =============
/**
 * @file peer_manager.cpp
 * @brief Implementation of P2P network connection management
 *
 * Implements TCP-based peer-to-peer networking with thread-per-connection
 * model, socket configuration (keep-alive, non-blocking), and connection
 * lifecycle management. Handles both inbound and outbound connections.
 */

#include "peer/peer_manager.h"
#include "peer/peer_message.h"
#include "utils/threadname.h"
#include "utils/hash.h"
#include "logger/logger.h"
#include <iostream>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <sstream>
#include <ctime>

// Platform-specific socket headers
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    // Don't define close as closesocket - Boost.Asio handles this
    #define SHUT_RDWR SD_BOTH
    // Helper function for closing raw sockets (not Boost.Asio wrapped ones)
    inline void close_socket(int sock) { closesocket(sock); }
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <errno.h>
    inline void close_socket(int sock) { close(sock); }
#endif

// ============= CPeerConnection Implementation =============

/**
 * @brief Default constructor - creates disconnected peer
 *
 * Initializes all fields to default values (no socket, no connection).
 */
CPeerConnection::CPeerConnection()
    : n_socket(-1), peer_node(std::make_shared<CPeerNode>()), f_connected(false), f_active(false), n_last_ping_nonce(0) {}

/**
 * @brief Construct peer with CPeerNode
 * @param node Peer node information
 *
 * Creates peer connection object from existing CPeerNode.
 * Does not establish connection - call ConnectToPeer() to actually connect.
 */
CPeerConnection::CPeerConnection(const CPeerNode& node)
    : n_socket(-1), peer_node(std::make_shared<CPeerNode>(node)), f_connected(false), f_active(false), n_last_ping_nonce(0) {}

/**
 * @brief Destructor - closes connection and cleans up resources
 *
 * Performs cleanup:
 * 1. Signal connection should stop (f_active = false)
 * 2. Close socket and mark invalid
 *
 * Note: Actual I/O is handled by Boost.Asio async infrastructure,
 * not per-connection threads.
 */
CPeerConnection::~CPeerConnection() {
    f_active = false;
    if (n_socket >= 0) {
        close_socket(n_socket);
        n_socket = -1;
    }
}

/**
 * @brief Move constructor for transferring ownership
 * @param other Peer connection to move from
 *
 * Transfers all resources (socket, peer node, flags) from other.
 * Resets other to disconnected state to prevent double-close.
 * Uses atomic load() to safely copy f_active flag.
 */
CPeerConnection::CPeerConnection(CPeerConnection&& other) noexcept
    : n_socket(other.n_socket),
      peer_node(std::move(other.peer_node)),
      f_connected(other.f_connected),
      f_active(other.f_active.load()),
      n_last_ping_nonce(other.n_last_ping_nonce),
      m_last_ping_send_time(other.m_last_ping_send_time) {
    other.n_socket = -1;
    other.f_connected = false;
    other.f_active = false;
    other.n_last_ping_nonce = 0;
}

/**
 * @brief Move assignment for transferring ownership
 * @param other Peer connection to move from
 * @return Reference to this
 *
 * Safely transfers resources by:
 * 1. Cleaning up existing resources (close socket)
 * 2. Moving resources from other
 * 3. Resetting other to prevent double-free
 *
 * Self-assignment check prevents resource destruction when this == &other.
 */
CPeerConnection& CPeerConnection::operator=(CPeerConnection&& other) noexcept {
    if (this != &other) {
        // Clean up existing resources
        if (n_socket >= 0) {
            close_socket(n_socket);
        }

        // Move from other
        n_socket = other.n_socket;
        peer_node = std::move(other.peer_node);
        f_connected = other.f_connected;
        f_active = other.f_active.load();
        n_last_ping_nonce = other.n_last_ping_nonce;
        m_last_ping_send_time = other.m_last_ping_send_time;

        // Reset other
        other.n_socket = -1;
        other.f_connected = false;
        other.f_active = false;
        other.n_last_ping_nonce = 0;
    }
    return *this;
}

// ============= CPeerManager Implementation =============

/**
 * @brief Construct peer manager with listening port and max peers
 * @param n_port Port to listen on for inbound connections
 * @param n_max_outbound Maximum number of outbound peer connections
 * @param n_max_inbound Maximum number of inbound peer connections
 * @param n_ping_time Interval in seconds between PING messages
 *
 * Initializes peer manager in stopped state. Reserves space for
 * peer connections to avoid vector reallocations during operation.
 * Call Start() to begin accepting connections.
 */
CPeerManager::CPeerManager(int n_port, int n_max_outbound, int n_max_inbound, int n_max_workers, int n_ping_time)
    : n_listen_port(n_port), n_listen_socket(-1),
      n_max_inbound_peers(n_max_inbound), n_max_outbound_peers(n_max_outbound),
      n_peers_ping_time(n_ping_time),
      f_running(false), f_stop_requested(false), f_stop_monitor(false),
      m_last_ping_time(std::chrono::steady_clock::now()),
      m_last_rotation_time(std::chrono::steady_clock::now()),
      p_blockweave(nullptr) {
    m_inbound_peers.reserve(n_max_inbound_peers);
    m_outbound_peers.reserve(n_max_outbound_peers);

    // Initialize Boost.Asio I/O infrastructure for inbound connections
    m_io_work = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
        boost::asio::make_work_guard(m_io_context));
    m_thread_pool = std::make_unique<boost::asio::thread_pool>(n_max_workers);

    LOG_INFO("Initialized Boost.Asio with " + std::to_string(n_max_workers) + " worker threads for " + std::to_string(n_max_inbound_peers) + " max inbound connections");
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
    m_peer_thread = std::thread(&CPeerManager::PeerManagerThread, this);

    // Start monitor thread for inbound socket I/O multiplexing
    m_monitor_inbound_thread = std::thread(&CPeerManager::MonitorInboundSocketThread, this);

    LOG_TRACE("Peer Manager started on port " + std::to_string(n_listen_port));
    LOG_TRACE("Maximum inbound peers: " + std::to_string(n_max_inbound_peers));
    LOG_TRACE("Maximum outbound peers: " + std::to_string(n_max_outbound_peers));

    return true;
}

/**
 * @brief Stop peer manager and all networking
 *
 * Shutdown sequence (thread-safe and idempotent):
 * 1. Check if running (safe to call when stopped)
 * 2. Set stop flags and stop Boost.Asio I/O infrastructure
 * 3. Close listening socket
 * 4. Cancel all async operations and close stream descriptors
 * 5. Signal all peer connections to stop (f_active = false)
 * 6. Shutdown and close all peer sockets
 * 7. Join listener, monitor, and peer management threads
 * 8. Join thread pool workers
 * 9. Clear peer lists (no per-connection threads to join since Boost.Asio migration)
 *
 * Blocks until all threads have terminated. Uses mutex to safely
 * access peer list during shutdown.
 */
void CPeerManager::Stop() {
    // Always clean up Boost.Asio resources, even if never started
    // This prevents hangs when CPeerManager is constructed but not started
    if (m_io_work) {
        m_io_work.reset();
    }
    m_io_context.stop();

    if (m_thread_pool) {
        m_thread_pool->join();
    }

    if (!f_running) {
        return;
    }

    LOG_INFO("Stopping peer manager");
    f_stop_requested = true;
    f_stop_monitor = true;
    f_running = false;

    // Close listen socket
    CloseListenSocket();

    // Stop all peer connections (both inbound and outbound)

    // First, cancel all async operations on inbound peers and close stream descriptors
    {
        std::lock_guard<std::mutex> lock(cs_inbound_descriptors);
        for (auto& [socket_fd, p_descriptor] : map_inbound_descriptors) {
            if (p_descriptor) {
                // Cancel pending async operations (will trigger operation_aborted)
                boost::system::error_code cancel_ec;
                p_descriptor->cancel(cancel_ec);

                // Close the descriptor
                boost::system::error_code close_ec;
                p_descriptor->close(close_ec);

                LOG_TRACE("Cancelled and closed async operations on socket " + std::to_string(socket_fd));
            }
        }
        map_inbound_descriptors.clear();
    }

    // Then close sockets and mark peers as inactive
    {
        std::lock_guard<std::mutex> lock(cs_peers);

        // Stop inbound peers (async I/O - descriptors already closed above)
        for (auto& p_peer : m_inbound_peers) {
            if (p_peer) {
                p_peer->f_active = false;
                p_peer->f_connected = false;
                if (p_peer->n_socket >= 0) {
                    // Socket may already be closed by descriptor, but ensure it's done
                    shutdown(p_peer->n_socket, SHUT_RDWR);
                    close(p_peer->n_socket);
                    p_peer->n_socket = -1;
                }
            }
        }

        // Stop outbound peers (async I/O - descriptors already closed above)
        for (auto& p_peer : m_outbound_peers) {
            if (p_peer) {
                p_peer->f_active = false;
                p_peer->f_connected = false;
                if (p_peer->n_socket >= 0) {
                    // Socket may already be closed by descriptor, but ensure it's done
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

    if (m_monitor_inbound_thread.joinable()) {
        m_monitor_inbound_thread.join();
    }

    // Thread pool already joined at the beginning of Stop()

    // Clear peer lists (all peers now use async I/O, no threads to join)
    {
        std::lock_guard<std::mutex> lock(cs_peers);

        // Both inbound and outbound peers use async I/O - no threads to join
        // Stream descriptors already closed above
        m_inbound_peers.clear();
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
#ifdef _WIN32
    setsockopt(n_listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&n_opt, sizeof(n_opt));
#else
    setsockopt(n_listen_socket, SOL_SOCKET, SO_REUSEADDR, &n_opt, sizeof(n_opt));
#endif

    // Bind socket
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(n_listen_port);

    if (bind(n_listen_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[Peer Manager] Failed to bind to port " << n_listen_port << "\n";
        close_socket(n_listen_socket);
        n_listen_socket = -1;
        return false;
    }

    // Listen
    if (listen(n_listen_socket, 10) < 0) {
        std::cerr << "[Peer Manager] Failed to listen\n";
        close_socket(n_listen_socket);
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
        close_socket(n_listen_socket);
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
#ifdef _WIN32
    // Windows: Enable keepalive and configure timing
    BOOL n_keepalive = TRUE;
    if (setsockopt(n_socket, SOL_SOCKET, SO_KEEPALIVE, (const char*)&n_keepalive, sizeof(n_keepalive)) < 0) {
        LOG_ERROR("Failed to set SO_KEEPALIVE");
        return false;
    }

    // Configure keepalive timing using tcp_keepalive structure
    struct tcp_keepalive keepalive_vals;
    keepalive_vals.onoff = 1;
    keepalive_vals.keepalivetime = 60000;     // 60 seconds (in milliseconds)
    keepalive_vals.keepaliveinterval = 10000; // 10 seconds (in milliseconds)

    DWORD bytes_returned;
    if (WSAIoctl(n_socket, SIO_KEEPALIVE_VALS, &keepalive_vals, sizeof(keepalive_vals),
                 NULL, 0, &bytes_returned, NULL, NULL) == SOCKET_ERROR) {
        LOG_WARN("Failed to set keepalive timing");
    }
#else
    // POSIX (Linux/macOS)
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
#endif

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
#ifdef _WIN32
    u_long mode = f_non_blocking ? 1 : 0;
    return ioctlsocket(n_socket, FIONBIO, &mode) == 0;
#else
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
#endif
}

/**
 * @brief Main peer management thread function
 *
 * Runs periodic maintenance tasks:
 * - Cleans up disconnected peers from peer list
 * - Sends PING messages to all connected peers every 30 seconds
 * - Runs every 5 seconds while peer manager is active
 *
 * Exits when f_stop_requested is set by Stop().
 */
void CPeerManager::PeerManagerThread() {
    // Set thread name for easier debugging and logging
    SetThreadName("peer_manager");

    LOG_INFO("Peer management thread started");

    while (!f_stop_requested) {
        // Clean up disconnected peers
        CleanupDisconnectedPeers();

        // Check if it's time to send PING messages (every n_peers_ping_time seconds)
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_last_ping_time);

        if (elapsed.count() >= n_peers_ping_time) {
            // Send PING to all connected peers (periodic keep-alive)
            SendPingToAllPeers();
            m_last_ping_time = now;
        }

        // Periodic maintenance tasks

        // Rotate outbound connections (every 30 minutes), enable it after AddressManager is implemented so it can establish new outbound connection
        // RotateOutboundConnections();

        // Clean up expired bans
        CleanupExpiredBans();

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
    // Set thread name for easier debugging and logging
    SetThreadName("peer_listener");

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

        // Check if peer is banned
        if (IsPeerBanned(std::string(str_ip))) {
            LOG_WARN("Rejecting connection from banned peer: " + std::string(str_ip));
            close(n_client_socket);
            continue;
        }

        // Check if we've reached max inbound peers
        {
            std::lock_guard<std::mutex> lock(cs_peers);
            if (m_inbound_peers.size() >= static_cast<size_t>(n_max_inbound_peers)) {
                // Don't reject - instead, drop a random peer from same subnet (if any)
                std::string str_new_subnet = GetSubnet(std::string(str_ip));

                // Find peers from the same subnet
                std::vector<size_t> same_subnet_indices;
                for (size_t i = 0; i < m_inbound_peers.size(); i++) {
                    if (m_inbound_peers[i] && m_inbound_peers[i]->f_connected) {
                        std::string str_peer_subnet = GetSubnet(m_inbound_peers[i]->peer_node->GetAddress());
                        if (str_peer_subnet == str_new_subnet) {
                            same_subnet_indices.push_back(i);
                        }
                    }
                }

                if (!same_subnet_indices.empty()) {
                    // Drop a random peer from the same subnet
                    std::srand(std::time(nullptr));
                    size_t random_idx = same_subnet_indices[std::rand() % same_subnet_indices.size()];

                    LOG_INFO("Max inbound peers reached. Dropping peer from same subnet " + str_new_subnet +
                             " to accept new connection: " + m_inbound_peers[random_idx]->peer_node->GetAddress());

                    DisconnectPeer(m_inbound_peers[random_idx].get());
                } else {
                    // No peer from same subnet - drop a random peer anyway for network diversity
                    std::vector<size_t> connected_indices;
                    for (size_t i = 0; i < m_inbound_peers.size(); i++) {
                        if (m_inbound_peers[i] && m_inbound_peers[i]->f_connected) {
                            connected_indices.push_back(i);
                        }
                    }

                    if (!connected_indices.empty()) {
                        std::srand(std::time(nullptr));
                        size_t random_idx = connected_indices[std::rand() % connected_indices.size()];

                        LOG_INFO("Max inbound peers reached. All peers from different subnets. "
                                 "Dropping random peer for network diversity: " +
                                 m_inbound_peers[random_idx]->peer_node->GetAddress() +
                                 " to accept new connection from " + std::string(str_ip));

                        DisconnectPeer(m_inbound_peers[random_idx].get());
                    } else {
                        // No connected peers - should not happen, but reject as fallback
                        LOG_ERROR("Maximum inbound peers reached but no connected peers found, add peer connection from " +
                                 std::string(str_ip) + ":" + std::to_string(n_peer_port));
                        // close(n_client_socket);
                        // continue;
                    }
                }
            }

            LOG_INFO("Accepted inbound peer connection from " + std::string(str_ip) + ":" + std::to_string(n_peer_port));

            // Set socket keepalive
            SetSocketKeepAlive(n_client_socket);

            // Create peer connection object for inbound peer (no thread for async I/O)
            CPeerNode node(std::string(str_ip), n_peer_port);
            auto p_peer = std::make_unique<CPeerConnection>(node);
            p_peer->n_socket = n_client_socket;
            p_peer->f_active = true;
            p_peer->f_connected = true;

            // Set connection_time for inbound peer
            int64_t n_now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            p_peer->peer_node->SetConnectionTime(n_now);
            p_peer->peer_node->SetInbound(true);  // Mark as inbound connection
            LOG_INFO("Set connection_time for inbound peer " + std::string(str_ip) + " to " + std::to_string(n_now));

            // Add to inbound peers list BEFORE registering socket
            m_inbound_peers.push_back(std::move(p_peer));
        }

        // Register socket with async I/O context (outside lock to avoid blocking)
        RegisterInboundSocket(n_client_socket, std::string(str_ip), n_peer_port);
        LOG_INFO("Registered inbound socket " + std::to_string(n_client_socket) + " with I/O context");
    }

    LOG_INFO("Peer listener thread stopped");
}

/**
 * @brief Monitor thread for inbound socket I/O multiplexing
 *
 * Runs Boost.Asio io_context event loop to monitor all inbound
 * sockets using select/epoll/poll (platform-specific).
 * Dispatches recv/send work to thread pool workers when sockets
 * have pending I/O operations.
 *
 * Uses io_context.run() which blocks until:
 * - All async operations complete AND
 * - io_work guard is destroyed (happens in Stop())
 *
 * Exits when f_stop_monitor is set by Stop().
 */
void CPeerManager::MonitorInboundSocketThread() {
    // Set thread name for easier debugging and logging
    SetThreadName("monitor_inbound");

    // Only one thread is enough, the kernel does the heavy lifting
    // monitor_inbound thread BLOCKS in io_context.run() (uses ~0% CPU)
    // Kernel notifies when ANY socket has data ready
    // The read itself is FAST (memcpy from kernel buffer)
    LOG_INFO("Monitor inbound socket thread started");

    while (!f_stop_monitor) {
        try {
            // Run the io_context event loop
            // This will block and process async I/O events
            // Returns when all work is done or io_context is stopped
            m_io_context.run();

            // If run() returns and we're not stopping, restart it
            if (!f_stop_monitor) {
#ifdef _WIN32
                // On Windows, io_context.run() can exit frequently when there are no pending async operations
                LOG_TRACE("io_context.run() exited unexpectedly, restarting...");
#else
                LOG_WARN("io_context.run() exited unexpectedly, restarting...");
#endif
                m_io_context.restart();
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR("Exception in MonitorInboundSocketThread: " + std::string(e.what()));

            if (!f_stop_monitor) {
                // Sleep briefly before retrying
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    LOG_INFO("Monitor inbound socket thread stopped");
}

/**
 * @brief Register inbound socket with async I/O context
 * @param n_socket_fd Socket file descriptor
 * @param str_address Peer IP address
 * @param n_port Peer port
 *
 * Wraps the socket file descriptor in a Boost.Asio stream_descriptor
 * and registers an async_read_some handler for non-blocking I/O.
 * The stream_descriptor takes ownership of the socket FD.
 *
 * This function is called by ListenerThread after accepting a new
 * inbound connection. The async I/O allows monitoring many sockets
 * with a single thread using select/epoll/poll.
 */
void CPeerManager::RegisterInboundSocket(int n_socket_fd, const std::string& str_address, int n_port) {
    try {
        // Create async socket descriptor to wrap the socket
        // This transfers ownership of the socket FD to Boost.Asio
#ifdef _WIN32
        // Windows: Use tcp::socket and assign native handle
        auto p_descriptor = std::make_shared<AsyncSocketDescriptor>(m_io_context);
        p_descriptor->assign(boost::asio::ip::tcp::v4(), n_socket_fd);
#else
        // POSIX: Use stream_descriptor with file descriptor
        auto p_descriptor = std::make_shared<AsyncSocketDescriptor>(m_io_context, n_socket_fd);
#endif

        // Store descriptor in map for later access
        {
            std::lock_guard<std::mutex> lock(cs_inbound_descriptors);
            map_inbound_descriptors[n_socket_fd] = p_descriptor;
        }

        LOG_INFO("Registered inbound socket " + std::to_string(n_socket_fd) +
                 " for async I/O (" + str_address + ":" + std::to_string(n_port) + ")");

        // Allocate read buffer (8KB for P2P messages)
        auto p_buffer = std::make_shared<std::vector<uint8_t>>(8192);

        // Start async read operation
        // When data arrives, HandleAsyncRead will be called
        // async_read_some() is NON-BLOCKING registration, not actual I/O
        // it registers read handler (non-blocking)
        p_descriptor->async_read_some(
            boost::asio::buffer(*p_buffer),
            [this, n_socket_fd, p_buffer](const boost::system::error_code& ec, size_t n_bytes_transferred) {
                HandleAsyncRead(ec, n_bytes_transferred, n_socket_fd, p_buffer);
            }
        );

        LOG_TRACE("Started async_read_some on socket " + std::to_string(n_socket_fd));
    }
    catch (const std::exception& e) {
        LOG_ERROR("Failed to register inbound socket " + std::to_string(n_socket_fd) + ": " + std::string(e.what()));

        // Clean up socket if registration failed
        close(n_socket_fd);
    }
}

/**
 * @brief Register outbound socket with async I/O context
 * @param n_socket_fd Socket file descriptor
 * @param str_address Peer IP address
 * @param n_port Peer port
 *
 * Wraps the socket file descriptor in a Boost.Asio stream_descriptor
 * and registers an async_read_some handler for non-blocking I/O.
 * The stream_descriptor takes ownership of the socket FD.
 *
 * This function is called by ConnectToPeer after establishing an
 * outbound connection. The async I/O allows monitoring both inbound
 * and outbound sockets with the same monitor thread.
 */
void CPeerManager::RegisterOutboundSocket(int n_socket_fd, const std::string& str_address, int n_port) {
    try {
        // Create async socket descriptor to wrap the socket
        // This transfers ownership of the socket FD to Boost.Asio
#ifdef _WIN32
        // Windows: Use tcp::socket and assign native handle
        auto p_descriptor = std::make_shared<AsyncSocketDescriptor>(m_io_context);
        p_descriptor->assign(boost::asio::ip::tcp::v4(), n_socket_fd);
#else
        // POSIX: Use stream_descriptor with file descriptor
        auto p_descriptor = std::make_shared<AsyncSocketDescriptor>(m_io_context, n_socket_fd);
#endif

        // Store descriptor in map for later access
        {
            std::lock_guard<std::mutex> lock(cs_inbound_descriptors);
            map_inbound_descriptors[n_socket_fd] = p_descriptor;
        }

        LOG_INFO("Registered outbound socket " + std::to_string(n_socket_fd) +
                 " for async I/O (" + str_address + ":" + std::to_string(n_port) + ")");

        // Allocate read buffer (8KB for P2P messages)
        auto p_buffer = std::make_shared<std::vector<uint8_t>>(8192);

        // Start async read operation
        // When data arrives, HandleAsyncRead will be called
        // async_read_some() is NON-BLOCKING registration, not actual I/O
        p_descriptor->async_read_some(
            boost::asio::buffer(*p_buffer),
            [this, n_socket_fd, p_buffer](const boost::system::error_code& ec, size_t n_bytes_transferred) {
                HandleAsyncRead(ec, n_bytes_transferred, n_socket_fd, p_buffer);
            }
        );

        LOG_TRACE("Started async_read_some on outbound socket " + std::to_string(n_socket_fd));
    }
    catch (const std::exception& e) {
        LOG_ERROR("Failed to register outbound socket " + std::to_string(n_socket_fd) + ": " + std::string(e.what()));

        // Clean up socket if registration failed
        close(n_socket_fd);
    }
}

/**
 * @brief Async read completion handler for inbound sockets
 * @param ec Boost error code from async operation
 * @param n_bytes_transferred Number of bytes read
 * @param n_socket_fd Socket file descriptor
 * @param p_buffer Shared pointer to read buffer
 *
 * Called by io_context when async_read_some completes.
 * Handles three cases:
 * 1. Success: Process received data and re-register for next read
 * 2. EOF: Peer disconnected gracefully, cleanup
 * 3. Error: Handle error and cleanup if needed
 *
 * This handler runs in the MonitorInboundSocketThread context.
 */
void CPeerManager::HandleAsyncRead(const boost::system::error_code& ec,
                                    size_t n_bytes_transferred,
                                    int n_socket_fd,
                                    std::shared_ptr<std::vector<uint8_t>> p_buffer) {

    // Check if socket descriptor still exists
    std::shared_ptr<AsyncSocketDescriptor> p_descriptor;
    {
        std::lock_guard<std::mutex> lock(cs_inbound_descriptors);
        auto it = map_inbound_descriptors.find(n_socket_fd);
        if (it == map_inbound_descriptors.end()) {
            // Socket already removed (disconnected)
            LOG_TRACE("HandleAsyncRead: Socket " + std::to_string(n_socket_fd) + " already removed");
            return;
        }
        p_descriptor = it->second;
    }

    // Handle error cases
    if (ec) {
        if (ec == boost::asio::error::eof) {
            LOG_INFO("Peer disconnected (EOF) on socket " + std::to_string(n_socket_fd));
        }
        else if (ec == boost::asio::error::operation_aborted) {
            LOG_TRACE("Async read aborted on socket " + std::to_string(n_socket_fd) + " (shutdown)");
        }
        else {
            LOG_ERROR("Async read error on socket " + std::to_string(n_socket_fd) + ": " + ec.message());
        }

        // Cleanup: Cancel pending operations, remove descriptor, close socket, and mark peer disconnected
        {
            std::lock_guard<std::mutex> lock(cs_inbound_descriptors);
            map_inbound_descriptors.erase(n_socket_fd);
        }

        // Cancel any pending async operations on this descriptor
        boost::system::error_code cancel_ec;
        p_descriptor->cancel(cancel_ec);

        // Close the descriptor (will close underlying socket)
        boost::system::error_code close_ec;
        p_descriptor->close(close_ec);

        // Mark the peer as disconnected so CleanupDisconnectedPeers() can remove it
        {
            std::lock_guard<std::mutex> lock(cs_peers);
            for (auto& p_peer : m_inbound_peers) {
                if (p_peer && p_peer->n_socket == n_socket_fd) {
                    p_peer->f_connected = false;
                    p_peer->f_active = false;
                    p_peer->n_socket = -1;
                    LOG_INFO("Marked inbound peer " + p_peer->peer_node->GetAddress() + " as disconnected");
                    break;
                }
            }
        }

        return;
    }

    // Success: Data received
    if (n_bytes_transferred > 0) {
        LOG_TRACE("Received " + std::to_string(n_bytes_transferred) +
                  " bytes on socket " + std::to_string(n_socket_fd));

        // Post work to thread pool to process the received data
        boost::asio::post(*m_thread_pool, [this, n_socket_fd, p_buffer, n_bytes_transferred]() {
            ProcessReceivedMessage(n_socket_fd, p_buffer, n_bytes_transferred);
        });
    }

    // Re-register for next async read (keep connection alive)
    try {
        // Allocate new buffer for next read
        auto p_new_buffer = std::make_shared<std::vector<uint8_t>>(8192);

        p_descriptor->async_read_some(
            boost::asio::buffer(*p_new_buffer),
            [this, n_socket_fd, p_new_buffer](const boost::system::error_code& ec, size_t n_bytes) {
                HandleAsyncRead(ec, n_bytes, n_socket_fd, p_new_buffer);
            }
        );

        LOG_TRACE("Re-registered async_read_some on socket " + std::to_string(n_socket_fd));
    }
    catch (const std::exception& e) {
        LOG_ERROR("Failed to re-register async read on socket " + std::to_string(n_socket_fd) +
                  ": " + std::string(e.what()));

        // Cleanup on failure: Cancel, remove, close, and mark disconnected
        {
            std::lock_guard<std::mutex> lock(cs_inbound_descriptors);
            map_inbound_descriptors.erase(n_socket_fd);
        }

        boost::system::error_code cancel_ec;
        p_descriptor->cancel(cancel_ec);

        boost::system::error_code close_ec;
        p_descriptor->close(close_ec);

        // Mark peer as disconnected
        {
            std::lock_guard<std::mutex> lock(cs_peers);
            for (auto& p_peer : m_inbound_peers) {
                if (p_peer && p_peer->n_socket == n_socket_fd) {
                    p_peer->f_connected = false;
                    p_peer->f_active = false;
                    p_peer->n_socket = -1;
                    LOG_INFO("Marked inbound peer " + p_peer->peer_node->GetAddress() + " as disconnected (re-register failed)");
                    break;
                }
            }
        }
    }
}

/**
 * @brief Send message asynchronously to inbound peer
 * @param n_socket_fd Socket file descriptor
 * @param message CPeerMessage to send
 *
 * Serializes the message and sends it asynchronously using Boost.Asio.
 * This is used ONLY for inbound peers (outbound peers use sync send).
 * Thread-safe operation with mutex protection.
 */
void CPeerManager::SendMessageAsync(int n_socket_fd, const CPeerMessage& message) {
    // Get descriptor for this socket
    std::shared_ptr<AsyncSocketDescriptor> p_descriptor;
    {
        std::lock_guard<std::mutex> lock(cs_inbound_descriptors);
        auto it = map_inbound_descriptors.find(n_socket_fd);
        if (it == map_inbound_descriptors.end()) {
            LOG_WARN("SendMessageAsync: Socket " + std::to_string(n_socket_fd) + " not found");
            return;
        }
        p_descriptor = it->second;
    }

    // Serialize message
    std::string str_serialized = message.Serialize();

    if (str_serialized.empty()) {
        LOG_ERROR("Failed to serialize message for socket " + std::to_string(n_socket_fd));
        return;
    }

    // Create shared buffer to keep data alive during async operation
    // Convert string to vector<uint8_t>
    auto p_buffer = std::make_shared<std::vector<uint8_t>>(str_serialized.begin(), str_serialized.end());

    // Send asynchronously
    boost::asio::async_write(
        *p_descriptor,
        boost::asio::buffer(*p_buffer),
        [this, n_socket_fd, p_buffer](const boost::system::error_code& ec, size_t n_bytes_transferred) {
            HandleAsyncWrite(ec, n_bytes_transferred, n_socket_fd);
        }
    );

    LOG_TRACE("Queued async write of " + std::to_string(p_buffer->size()) +
              " bytes to socket " + std::to_string(n_socket_fd));
}

/**
 * @brief Async write completion handler for inbound sockets
 * @param ec Boost error code from async operation
 * @param n_bytes_transferred Number of bytes written
 * @param n_socket_fd Socket file descriptor
 *
 * Called when async_write completes (successfully or with error).
 * Logs result and handles errors by closing the connection.
 */
void CPeerManager::HandleAsyncWrite(const boost::system::error_code& ec,
                                     size_t n_bytes_transferred,
                                     int n_socket_fd) {
    if (ec) {
        if (ec == boost::asio::error::operation_aborted) {
            LOG_TRACE("Async write aborted on socket " + std::to_string(n_socket_fd) + " (shutdown)");
        }
        else {
            LOG_ERROR("Async write error on socket " + std::to_string(n_socket_fd) + ": " + ec.message());
        }

        // On error: Cancel pending operations, remove descriptor, close socket, and mark peer disconnected
        std::shared_ptr<AsyncSocketDescriptor> p_descriptor;
        {
            std::lock_guard<std::mutex> lock(cs_inbound_descriptors);
            auto it = map_inbound_descriptors.find(n_socket_fd);
            if (it != map_inbound_descriptors.end()) {
                p_descriptor = it->second;
                map_inbound_descriptors.erase(it);
            }
        }

        if (p_descriptor) {
            boost::system::error_code cancel_ec;
            p_descriptor->cancel(cancel_ec);

            boost::system::error_code close_ec;
            p_descriptor->close(close_ec);
        }

        // Mark peer as disconnected
        {
            std::lock_guard<std::mutex> lock(cs_peers);
            for (auto& p_peer : m_inbound_peers) {
                if (p_peer && p_peer->n_socket == n_socket_fd) {
                    p_peer->f_connected = false;
                    p_peer->f_active = false;
                    p_peer->n_socket = -1;
                    LOG_INFO("Marked inbound peer " + p_peer->peer_node->GetAddress() + " as disconnected (write error)");
                    break;
                }
            }
        }

        return;
    }

    // Success
    LOG_TRACE("Async write completed: " + std::to_string(n_bytes_transferred) +
              " bytes sent on socket " + std::to_string(n_socket_fd));
}

/**
 * @brief Process received message from inbound peer
 * @param n_socket_fd Socket file descriptor
 * @param p_buffer Shared pointer to buffer containing received data
 * @param n_bytes_received Number of bytes in buffer
 *
 * Processes messages from inbound async connections.
 * Handles PING, PONG, and other message types.
 * Posted to thread pool for parallel processing.
 */
void CPeerManager::ProcessReceivedMessage(int n_socket_fd,
                                          std::shared_ptr<std::vector<uint8_t>> p_buffer,
                                          size_t n_bytes_received) {
    // Set thread name for worker thread using integer ID (like rest_worker0, rest_worker1, etc.)
    static std::atomic<int> s_worker_id_counter{0};
    static thread_local int s_worker_id = s_worker_id_counter.fetch_add(1);
    static thread_local bool thread_name_set = false;
    if (!thread_name_set) {
        // at process level, the thread name can only be changed when boost 
        // calls this function, because this thread is maintained by boost
        SetThreadName("p2p_worker" + std::to_string(s_worker_id));
        thread_name_set = true;
    }

    LOG_TRACE("Processing " + std::to_string(n_bytes_received) + " bytes from socket " + std::to_string(n_socket_fd));

    // Find the peer connection for this socket (search both inbound and outbound lists)
    CPeerConnection* p_peer = nullptr;
    {
        std::lock_guard<std::mutex> lock(cs_peers);

        // Search inbound peers first
        for (auto& peer : m_inbound_peers) {
            if (peer && peer->n_socket == n_socket_fd) {
                p_peer = peer.get();
                break;
            }
        }

        // If not found in inbound, search outbound peers
        if (!p_peer) {
            for (auto& peer : m_outbound_peers) {
                if (peer && peer->n_socket == n_socket_fd) {
                    p_peer = peer.get();
                    break;
                }
            }
        }
    }

    if (!p_peer) {
        LOG_WARN("ProcessReceivedMessage: Could not find peer for socket " + std::to_string(n_socket_fd));
        return;
    }

    // Convert buffer to string for message parsing
    // Note: In production, we should handle partial messages and buffering per-socket
    std::string str_data(p_buffer->begin(), p_buffer->begin() + n_bytes_received);

    // Try to deserialize complete messages from buffer
    // CPeerMessage format: [1 byte type_length][N bytes type][4 bytes payload_length][M bytes payload]
    while (str_data.size() >= CPeerMessage::GetMinHeaderSize()) {
        // Try to deserialize a message
        CPeerMessage received_msg;
        if (received_msg.Deserialize(str_data)) {
            // Successfully deserialized a message
            std::string msg_type = received_msg.GetType();
            LOG_TRACE("Received " + msg_type + " message from peer " + p_peer->peer_node->GetAddress());

            // Calculate message size and remove from buffer
            // Message size = 1 (type_length) + type_length + 4 (payload_length) + payload_length
            size_t type_len = static_cast<uint8_t>(str_data[0]);
            size_t msg_size = 1 + type_len + 4 + received_msg.GetPayloadSize();
            str_data.erase(0, msg_size);

            // Handle different message types
            if (msg_type == MessageType::PING) {
                HandlePingMessage(p_peer, received_msg, n_socket_fd);
            } else if (msg_type == MessageType::PONG) {
                HandlePongMessage(p_peer, received_msg);
            } else if (msg_type == MessageType::TX_IDS) {
                HandleTxIdsMessage(p_peer, received_msg);
            } else if (msg_type == MessageType::INVENTORY) {
                HandleInventoryMessage(p_peer, received_msg);
            } else if (msg_type == MessageType::VERSION) {
                HandleVersionMessage(p_peer, received_msg);
            } else if (msg_type == MessageType::GETDATA) {
                HandleGetDataMessage(p_peer, received_msg);
            } else if (msg_type == MessageType::TXS) {
                HandleTxsMessage(p_peer, received_msg);
            } else if (msg_type == MessageType::BLOCKS) {
                HandleBlocksMessage(p_peer, received_msg);
            } else {
                LOG_TRACE("Received unknown message type '" + msg_type + "' from peer " + p_peer->peer_node->GetAddress());
            }
        } else {
            // Not enough data yet for a complete message, wait for more
            // Note: In production, we should buffer this partial data per-socket
            LOG_TRACE("Incomplete message in buffer, waiting for more data (socket " + std::to_string(n_socket_fd) + ")");
            break;
        }
    }
}

// ============================================================================
// Message Handler Helper Methods
// ============================================================================

void CPeerManager::HandlePingMessage(CPeerConnection* p_peer, const CPeerMessage& received_msg, int n_socket_fd) {
    // Extract nonce from PING payload
    const std::vector<uint8_t>& payload = received_msg.GetPayloadBytes();
    uint32_t n_nonce = 0;
    if (payload.size() >= 4) {
        n_nonce = (static_cast<uint32_t>(payload[0]) << 24) |
                 (static_cast<uint32_t>(payload[1]) << 16) |
                 (static_cast<uint32_t>(payload[2]) << 8) |
                 static_cast<uint32_t>(payload[3]);
        LOG_INFO("Received PING with nonce " + std::to_string(n_nonce) +
                " from peer " + p_peer->peer_node->GetAddress() + ", sending PONG");
    } else {
        LOG_WARN("Unexpected PING from peer " + p_peer->peer_node->GetAddress() + ", ignore");
        return;
    }

    // Respond with PONG containing the same nonce
    CPeerMessage pong_msg(MessageType::PONG, payload);

    // Send PONG via async I/O
    SendMessageAsync(n_socket_fd, pong_msg);

    LOG_TRACE("Queued PONG response with nonce " + std::to_string(n_nonce) +
             " to peer " + p_peer->peer_node->GetAddress());
}

void CPeerManager::HandlePongMessage(CPeerConnection* p_peer, const CPeerMessage& received_msg) {
    // Extract nonce from PONG payload
    const std::vector<uint8_t>& payload = received_msg.GetPayloadBytes();
    if (payload.size() >= 4) {
        uint32_t n_nonce = (static_cast<uint32_t>(payload[0]) << 24) |
                          (static_cast<uint32_t>(payload[1]) << 16) |
                          (static_cast<uint32_t>(payload[2]) << 8) |
                          static_cast<uint32_t>(payload[3]);

        // Verify nonce matches last sent PING
        if (n_nonce == p_peer->n_last_ping_nonce) {
            // Calculate round-trip time
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                now - p_peer->m_last_ping_send_time);
            double d_roundtrip_ms = duration.count() / 1000.0;

            // Update peer node with ping round-trip time
            p_peer->peer_node->SetPingRoundtripTime(d_roundtrip_ms);

            LOG_INFO("Received PONG from peer " + p_peer->peer_node->GetAddress() +
                    " with matching nonce " + std::to_string(n_nonce) +
                    ", round-trip time: " + std::to_string(d_roundtrip_ms) + " ms");
        } else {
            LOG_WARN("Received PONG from peer " + p_peer->peer_node->GetAddress() +
                    " with nonce " + std::to_string(n_nonce) +
                    " but expected " + std::to_string(p_peer->n_last_ping_nonce));
        }
    } else {
        LOG_TRACE("Received PONG from peer " + p_peer->peer_node->GetAddress() + " (no nonce)");
    }
}

void CPeerManager::HandleTxIdsMessage(CPeerConnection* p_peer, const CPeerMessage& received_msg) {
    std::string str_tx_ids = received_msg.GetPayloadString();
    LOG_INFO("Received transaction IDs from peer " + p_peer->peer_node->GetAddress() + ": " + str_tx_ids.substr(0, 64) + "...");

    // Parse comma-separated transaction IDs
    std::vector<std::string> vec_tx_ids;
    size_t n_start = 0;
    size_t n_comma_pos = str_tx_ids.find(',');

    while (n_comma_pos != std::string::npos) {
        std::string str_tx_id = str_tx_ids.substr(n_start, n_comma_pos - n_start);
        if (!str_tx_id.empty()) {
            vec_tx_ids.push_back(str_tx_id);
        }
        n_start = n_comma_pos + 1;
        n_comma_pos = str_tx_ids.find(',', n_start);
    }

    // Don't forget the last one
    if (n_start < str_tx_ids.length()) {
        std::string str_tx_id = str_tx_ids.substr(n_start);
        if (!str_tx_id.empty()) {
            vec_tx_ids.push_back(str_tx_id);
        }
    }

    LOG_INFO("Parsed " + std::to_string(vec_tx_ids.size()) + " transaction IDs from peer " +
             p_peer->peer_node->GetAddress());

    // TODO: In Phase 2.1, we'll implement GETDATA message to request missing transactions
    // For now, we just log the received transaction IDs
}

void CPeerManager::HandleInventoryMessage(CPeerConnection* p_peer, const CPeerMessage& received_msg) {
    std::string str_inventory = received_msg.GetPayloadString();
    LOG_INFO("Received INVENTORY from peer " + p_peer->peer_node->GetAddress() + ": " +
             std::to_string(str_inventory.length()) + " bytes");

    // Parse inventory format: [count:4bytes][type:2bytes][hash:32bytes][type:2bytes][hash:32bytes]...
    // Count: MESSAGE_COUNT_SIZE bytes (uint32_t, network byte order)
    // For each item: Type (MESSAGE_TYPE_SIZE bytes, ObjectType::Type) + Hash (MESSAGE_HASH_HEX_SIZE bytes, SHA-256 hex string)

    if (str_inventory.length() < MESSAGE_COUNT_SIZE) {
        LOG_ERROR("Invalid INVENTORY message from peer " + p_peer->peer_node->GetAddress() +
                 ": too short (need at least count field)");
        return;
    }

    // Parse count
    uint32_t n_count;
    std::memcpy(&n_count, str_inventory.data(), MESSAGE_COUNT_SIZE);
    n_count = ntohl(n_count);  // Convert from network byte order

    LOG_INFO("INVENTORY count: " + std::to_string(n_count));

    // Validate total length: MESSAGE_COUNT_SIZE + n_count * (MESSAGE_TYPE_SIZE + MESSAGE_HASH_SIZE)
    const size_t n_item_size = MESSAGE_TYPE_SIZE + MESSAGE_HASH_SIZE;
    const size_t n_expected_length = MESSAGE_COUNT_SIZE + (n_count * n_item_size);

    if (str_inventory.length() < n_expected_length) {
        LOG_ERROR("Invalid INVENTORY message from peer " + p_peer->peer_node->GetAddress() +
                 ": expected " + std::to_string(n_expected_length) + " bytes, got " +
                 std::to_string(str_inventory.length()));
        return;
    }

    // Track all inventory items for relay
    std::vector<std::pair<ObjectType::Type, std::string>> vec_inventory;

    // Track missing items for GETDATA request
    std::vector<std::pair<ObjectType::Type, std::string>> vec_missing_items;

    // Parse each inventory item
    size_t n_offset = MESSAGE_COUNT_SIZE;  // Skip count field
    for (uint32_t i = 0; i < n_count; i++) {
        // Read type (MESSAGE_TYPE_SIZE bytes)
        ObjectType::Type obj_type = ReadObjectType(str_inventory.data() + n_offset);
        // TODO check VERSION to decide whether this is within the expected object type range
        if (obj_type <= ObjectType::OBJ_BEGIN || obj_type >= ObjectType::OBJ_LAST) {
            LOG_ERROR("Invalid INVENTORY message from peer " + p_peer->peer_node->GetAddress() +
             ": object type unknown " + std::to_string(obj_type));
            return;
        }
        n_offset += MESSAGE_TYPE_SIZE;

        // Read hash (MESSAGE_HASH_SIZE bytes, binary format)
        std::string str_hash(str_inventory.data() + n_offset, MESSAGE_HASH_SIZE);
        n_offset += MESSAGE_HASH_SIZE;

        // Add to inventory list for relay
        vec_inventory.push_back({obj_type, str_hash});

        // Mark this peer as knowing about this inventory
        MarkInventoryKnown(p_peer->peer_node, obj_type, str_hash);

        // Smart filtering: only request if we don't already have it
        bool f_need_item = false;
        if (p_blockweave) {
            if (obj_type == ObjectType::TRANSACTION) {
                // Check if transaction is in mempool (expects hex string)
                CHash hash(reinterpret_cast<const unsigned char*>(str_hash.data()), str_hash.size());
                f_need_item = !p_blockweave->HasTransactionInMempool(hash.GetData());
            } else if (obj_type == ObjectType::BLOCK) {
                // Check if block exists in blockchain
                CHash hash(reinterpret_cast<const unsigned char*>(str_hash.data()), str_hash.size());
                f_need_item = (p_blockweave->GetBlock(hash) == nullptr);
            }
        } else {
            // No blockweave access - request everything (conservative)
            f_need_item = true;
        }

        if (f_need_item) {
            vec_missing_items.push_back({obj_type, str_hash});
        }
    }

    LOG_INFO("Processed " + std::to_string(n_count) + " inventory items from peer " +
             p_peer->peer_node->GetAddress() + " (" +
             std::to_string(vec_missing_items.size()) + " missing, " +
             std::to_string(n_count - vec_missing_items.size()) + " already have)");

    // Relay to other peers who don't know about it (scheduled asynchronously)
    if (!vec_inventory.empty()) {
        std::vector<std::pair<ObjectType::Type, std::string>> inventory = vec_inventory;
        boost::asio::post(*m_thread_pool, [this, inventory]() {
            BroadcastInventory(inventory);
        });
    }

    // Request missing items via GETDATA (scheduled asynchronously)
    if (!vec_missing_items.empty()) {
        int socket = p_peer->n_socket;
        std::vector<std::pair<ObjectType::Type, std::string>> items = vec_missing_items;
        size_t item_count = items.size();
        std::string peer_addr = p_peer->peer_node->GetAddress();

        boost::asio::post(*m_thread_pool, [this, socket, items, item_count, peer_addr]() {
            ScheduleGetDataMessage(socket, items);
            LOG_INFO("Sent GETDATA request for " + std::to_string(item_count) +
                     " items to peer " + peer_addr);
        });
    } else {
        LOG_TRACE("No GETDATA needed - already have all inventory items from peer " +
                  p_peer->peer_node->GetAddress());
    }
}

void CPeerManager::HandleVersionMessage(CPeerConnection* p_peer, const CPeerMessage& received_msg) {
    std::string str_version_info = received_msg.GetPayloadString();
    LOG_INFO("Received VERSION from peer " + p_peer->peer_node->GetAddress() + ": " +
             str_version_info);

    // Store version info in peer node
    // p_peer->peer_node->SetVersion(str_version_info);

    LOG_INFO("Peer " + p_peer->peer_node->GetAddress() + " version handshake received");
}

void CPeerManager::HandleGetDataMessage(CPeerConnection* p_peer, const CPeerMessage& received_msg) {
    std::string str_getdata = received_msg.GetPayloadString();
    LOG_INFO("Received GETDATA from peer " + p_peer->peer_node->GetAddress() + ": " +
             std::to_string(str_getdata.length()) + " bytes");

    // Parse GETDATA message in binary format: [count:4][type:2][hash:32]...
    // Minimum size: MESSAGE_COUNT_SIZE bytes for count
    if (str_getdata.length() < MESSAGE_COUNT_SIZE) {
        LOG_WARN("Received invalid GETDATA from peer " + p_peer->peer_node->GetAddress() +
                 ": payload too short (expected at least " + std::to_string(MESSAGE_COUNT_SIZE) +
                 " bytes, got " + std::to_string(str_getdata.length()) + ")");
        return;
    }

    // Read count (MESSAGE_COUNT_SIZE bytes, network byte order)
    uint32_t n_count_network;
    std::memcpy(&n_count_network, str_getdata.data(), MESSAGE_COUNT_SIZE);
    uint32_t n_count = ntohl(n_count_network);

    // Validate count and payload size
    size_t n_expected_size = MESSAGE_COUNT_SIZE + n_count * (MESSAGE_TYPE_SIZE + MESSAGE_HASH_SIZE);
    if (str_getdata.length() != n_expected_size) {
        LOG_WARN("Received invalid GETDATA from peer " + p_peer->peer_node->GetAddress() +
                 ": size mismatch (expected " + std::to_string(n_expected_size) +
                 " bytes for " + std::to_string(n_count) + " items, got " +
                 std::to_string(str_getdata.length()) + ")");
        return;
    }

    // Parse requested items
    std::vector<std::pair<ObjectType::Type, std::string>> vec_requested_items;
    size_t n_offset = MESSAGE_COUNT_SIZE;

    for (uint32_t i = 0; i < n_count; ++i) {
        // Read type (MESSAGE_TYPE_SIZE bytes)
        ObjectType::Type obj_type = ReadObjectType(str_getdata.data() + n_offset);
        if (obj_type <= ObjectType::OBJ_BEGIN || obj_type >= ObjectType::OBJ_LAST) {
            LOG_WARN("Received invalid GETDATA from peer " + p_peer->peer_node->GetAddress() +
                     ": unknown object type " + std::to_string(obj_type) + " at item " + std::to_string(i));
            return;
        }
        n_offset += MESSAGE_TYPE_SIZE;

        // Read hash (MESSAGE_HASH_SIZE bytes, binary format)
        std::string str_hash(str_getdata.data() + n_offset, MESSAGE_HASH_SIZE);
        n_offset += MESSAGE_HASH_SIZE;

        vec_requested_items.push_back({obj_type, str_hash});
    }

    LOG_INFO("Peer " + p_peer->peer_node->GetAddress() + " requested " +
             std::to_string(vec_requested_items.size()) + " items via GETDATA");

    // Schedule response asynchronously to avoid blocking
    int socket = p_peer->n_socket;
    std::string peer_addr = p_peer->peer_node->GetAddress();
    std::vector<std::pair<ObjectType::Type, std::string>> requested_items = vec_requested_items;

    boost::asio::post(*m_thread_pool, [this, socket, peer_addr, requested_items]() {
        // Batch serialize transactions and blocks separately
        std::vector<std::string> vec_serialized_txs;
        std::vector<std::string> vec_serialized_blocks;

        // Serialize all requested items
        for (const auto& item : requested_items) {
            ObjectType::Type obj_type = item.first;
            const std::string& str_hash = item.second;
            CHash hash(reinterpret_cast<const unsigned char*>(str_hash.data()), str_hash.size());
            LOG_TRACE("Peer " + peer_addr + " requested object type: " + std::to_string(obj_type) + " via GETDATA");

            if (obj_type == ObjectType::TRANSACTION) {
                // Query transaction from blockweave
                if (p_blockweave) {
                    std::shared_ptr<CTransaction> p_tx = p_blockweave->GetTransactionFromMempool(hash);
                    if (p_tx) {
                        vec_serialized_txs.push_back(p_tx->Serialize());
                        LOG_TRACE("Serialized transaction " + hash.GetData().substr(0, 16) + "... for peer " + peer_addr);
                    } else {
                        LOG_TRACE("Transaction " + hash.GetData().substr(0, 16) + "... not found in mempool for peer " + peer_addr);
                    }
                }
            } else if (obj_type == ObjectType::BLOCK) {
                // Query block from blockweave
                if (p_blockweave) {
                    std::shared_ptr<CBlock> p_block = p_blockweave->GetBlock(hash);
                    if (p_block) {
                        vec_serialized_blocks.push_back(p_block->Serialize());
                        LOG_TRACE("Serialized block " + hash.GetData().substr(0, 16) + "... for peer " + peer_addr); } else {
                        LOG_TRACE("Block " + hash.GetData().substr(0, 16) + "... not found for peer " + peer_addr);
                    }
                }
            }
        }

        // Send batched TXS message if we have transactions
        if (!vec_serialized_txs.empty()) {
            std::string str_batched_txs;
            // Write count
            uint32_t n_tx_count = htonl(static_cast<uint32_t>(vec_serialized_txs.size()));
            str_batched_txs.append(reinterpret_cast<const char*>(&n_tx_count), BLOCK_UINT32_SIZE);
            // Write each transaction (length + data)
            for (const auto& str_tx : vec_serialized_txs) {
                uint32_t n_tx_len = htonl(static_cast<uint32_t>(str_tx.length()));
                str_batched_txs.append(reinterpret_cast<const char*>(&n_tx_len), BLOCK_UINT32_SIZE);
                str_batched_txs.append(str_tx);
            }
            CPeerMessage txs_msg(MessageType::TXS, str_batched_txs);
            SendMessageAsync(socket, txs_msg);
            LOG_TRACE("Sent batched TXS message with " + std::to_string(vec_serialized_txs.size()) + " transactions to peer " + peer_addr);
        }

        // Send batched BLOCKS message if we have blocks
        if (!vec_serialized_blocks.empty()) {
            std::string str_batched_blocks;
            // Write count
            uint32_t n_block_count = htonl(static_cast<uint32_t>(vec_serialized_blocks.size()));
            str_batched_blocks.append(reinterpret_cast<const char*>(&n_block_count), BLOCK_UINT32_SIZE);
            // Write each block (length + data)
            for (const auto& str_block : vec_serialized_blocks) {
                uint32_t n_block_len = htonl(static_cast<uint32_t>(str_block.length()));
                str_batched_blocks.append(reinterpret_cast<const char*>(&n_block_len), BLOCK_UINT32_SIZE);
                str_batched_blocks.append(str_block);
            }
            CPeerMessage blocks_msg(MessageType::BLOCKS, str_batched_blocks);
            SendMessageAsync(socket, blocks_msg);
            LOG_TRACE("Sent batched BLOCKS message with " + std::to_string(vec_serialized_blocks.size()) + " blocks to peer " + peer_addr);
        }
    });
}

void CPeerManager::HandleTxsMessage(CPeerConnection* p_peer, const CPeerMessage& received_msg) {
    std::string str_tx_data = received_msg.GetPayloadString();
    LOG_INFO("Received TXS from peer " + p_peer->peer_node->GetAddress() + ": " +
             std::to_string(str_tx_data.length()) + " bytes");

    // Parse batched TXS message: [count:4][tx1_len:4][tx1_data]...[txN_len:4][txN_data]
    if (str_tx_data.length() < BLOCK_UINT32_SIZE) {
        LOG_WARN("Received invalid TXS from peer " + p_peer->peer_node->GetAddress() + ": too short");
        return;
    }

    size_t n_offset = 0;
    // Read transaction count
    uint32_t n_tx_count_network;
    std::memcpy(&n_tx_count_network, str_tx_data.data() + n_offset, BLOCK_UINT32_SIZE);
    uint32_t n_tx_count = ntohl(n_tx_count_network);
    n_offset += BLOCK_UINT32_SIZE;

    LOG_INFO("Batched TXS message contains " + std::to_string(n_tx_count) + " transactions");

    std::vector<std::pair<ObjectType::Type, std::string>> vec_inventory;

    // Deserialize each transaction
    for (uint32_t i = 0; i < n_tx_count && n_offset < str_tx_data.length(); ++i) {
        // Read transaction length
        if (n_offset + BLOCK_UINT32_SIZE > str_tx_data.length()) {
            LOG_WARN("Invalid TXS message from peer " + p_peer->peer_node->GetAddress() + ": truncated at tx " + std::to_string(i));
            break;
        }
        uint32_t n_tx_len_network;
        std::memcpy(&n_tx_len_network, str_tx_data.data() + n_offset, BLOCK_UINT32_SIZE);
        uint32_t n_tx_len = ntohl(n_tx_len_network);
        n_offset += BLOCK_UINT32_SIZE;

        // Read transaction data
        if (n_offset + n_tx_len > str_tx_data.length()) {
            LOG_WARN("Invalid TXS message from peer " + p_peer->peer_node->GetAddress() + ": insufficient data for tx " + std::to_string(i));
            break;
        }
        std::string str_single_tx(str_tx_data.data() + n_offset, n_tx_len);
        n_offset += n_tx_len;

        // Deserialize transaction
        std::shared_ptr<CTransaction> p_tx = CTransaction::Deserialize(str_single_tx);
        if (!p_tx) {
            LOG_WARN("Failed to deserialize transaction " + std::to_string(i) + " from peer " + p_peer->peer_node->GetAddress());
        } else {
            LOG_INFO("Successfully deserialized transaction " + p_tx->m_id.GetData().substr(0, 16) +
                     "... from peer " + p_peer->peer_node->GetAddress());

            // Add transaction to blockweave mempool
            if (p_blockweave) {
                p_blockweave->AddTransaction(p_tx);
                LOG_INFO("Added transaction " + p_tx->m_id.GetData().substr(0, 16) + "... to mempool");

                // Collect for inventory broadcast
                vec_inventory.push_back({ObjectType::TRANSACTION, p_tx->m_id.GetData()});
            }
        }
    }

    // Broadcast INVENTORY for all new transactions (scheduled asynchronously)
    if (!vec_inventory.empty()) {
        boost::asio::post(*m_thread_pool, [this, vec_inventory]() {
            BroadcastInventory(vec_inventory);
            LOG_TRACE("Broadcasted INVENTORY for " + std::to_string(vec_inventory.size()) + " received transactions");
        });
    }
}

void CPeerManager::HandleBlocksMessage(CPeerConnection* p_peer, const CPeerMessage& received_msg) {
    std::string str_block_data = received_msg.GetPayloadString();
    LOG_INFO("Received BLOCKS from peer " + p_peer->peer_node->GetAddress() + ": " +
             std::to_string(str_block_data.length()) + " bytes");

    // Parse batched BLOCKS message: [count:4][block1_len:4][block1_data]...[blockN_len:4][blockN_data]
    if (str_block_data.length() < BLOCK_UINT32_SIZE) {
        LOG_WARN("Received invalid BLOCKS from peer " + p_peer->peer_node->GetAddress() + ": too short");
        return;
    }

    size_t n_offset = 0;
    // Read block count
    uint32_t n_block_count_network;
    std::memcpy(&n_block_count_network, str_block_data.data() + n_offset, BLOCK_UINT32_SIZE);
    uint32_t n_block_count = ntohl(n_block_count_network);
    n_offset += BLOCK_UINT32_SIZE;

    LOG_INFO("Batched BLOCKS message contains " + std::to_string(n_block_count) + " blocks");

    // Deserialize each block
    for (uint32_t i = 0; i < n_block_count && n_offset < str_block_data.length(); ++i) {
        // Read block length
        if (n_offset + BLOCK_UINT32_SIZE > str_block_data.length()) {
            LOG_WARN("Invalid BLOCKS message from peer " + p_peer->peer_node->GetAddress() + ": truncated at block " + std::to_string(i));
            break;
        }
        uint32_t n_block_len_network;
        std::memcpy(&n_block_len_network, str_block_data.data() + n_offset, BLOCK_UINT32_SIZE);
        uint32_t n_block_len = ntohl(n_block_len_network);
        n_offset += BLOCK_UINT32_SIZE;

        // Read block data
        if (n_offset + n_block_len > str_block_data.length()) {
            LOG_WARN("Invalid BLOCKS message from peer " + p_peer->peer_node->GetAddress() + ": insufficient data for block " + std::to_string(i));
            break;
        }
        std::string str_single_block(str_block_data.data() + n_offset, n_block_len);
        n_offset += n_block_len;

        // Deserialize block
        std::shared_ptr<CBlock> p_block = CBlock::Deserialize(str_single_block);
        if (!p_block) {
            LOG_WARN("Failed to deserialize block " + std::to_string(i) + " from peer " + p_peer->peer_node->GetAddress());
        } else {
            LOG_INFO("Successfully deserialized block #" + std::to_string(p_block->GetHeight()) +
                     " (hash: " + p_block->GetHash().GetData().substr(0, 16) + "...) from peer " +
                     p_peer->peer_node->GetAddress());

            // TODO: Add block validation and storage to blockweave
            // For now, just log receipt. Full block acceptance logic requires:
            // - Validating proof-of-work
            // - Checking block height sequencing
            // - Verifying transactions
            // - Adding to blockchain if valid
            // This is beyond the scope of GETDATA implementation

            LOG_TRACE("Block contains " + std::to_string(p_block->GetTransactions().size()) +
                     " transactions");

            // If block is valid, broadcast INVENTORY to other peers
            // Commented out until block validation is implemented:
            // std::vector<std::pair<ObjectType::Type, std::string>> vec_inventory;
            // vec_inventory.push_back({ObjectType::BLOCK, p_block->GetHash().GetData()});
            // BroadcastInventory(vec_inventory);
        }
    }
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
 * 5. Set socket to non-blocking mode (required for async I/O)
 * 6. Create CPeerConnection object with socket
 * 7. Register socket with async I/O context (same as inbound)
 * 8. Add to outbound peers list (mutex-protected)
 *
 * On any error, closes socket and returns false.
 * This is a private method called by AddPeer() after validation.
 *
 * Note: Since Boost.Asio migration, outbound peers use the same async I/O
 * infrastructure as inbound peers (no dedicated thread per connection).
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
        close_socket(n_socket);
        return false;
    }

    if (connect(n_socket, (sockaddr*)&peer_addr, sizeof(peer_addr)) < 0) {
        LOG_ERROR("Failed to connect to peer " + str_address + ":" + std::to_string(n_port) + " - " + strerror(errno));
        close_socket(n_socket);
        return false;
    }

    // Set socket to non-blocking mode (required for Boost.Asio async I/O)
    if (!SetSocketNonBlocking(n_socket, true)) {
        LOG_ERROR("Failed to set non-blocking mode for peer " + str_address);
        close_socket(n_socket);
        return false;
    }

    // Create peer connection object
    CPeerNode node(str_address, n_port);
    auto p_peer = std::make_unique<CPeerConnection>(node);
    p_peer->n_socket = n_socket;
    p_peer->f_connected = true;
    p_peer->f_active = true;

    // Set connection_time to current timestamp
    int64_t n_now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    p_peer->peer_node->SetConnectionTime(n_now);
    p_peer->peer_node->SetInbound(false);  // Mark as outbound connection
    LOG_INFO("Set connection_time for outbound peer " + str_address + " to " + std::to_string(n_now));

    // Register outbound socket with async I/O context (same infrastructure as inbound)
    RegisterOutboundSocket(n_socket, str_address, n_port);

    // Replace the placeholder (nullptr) in the outbound peers list with the actual peer
    // The placeholder was added by AddPeer() to reserve the slot
    {
        std::lock_guard<std::mutex> lock(cs_peers);
        // Find the first nullptr (placeholder) and replace it
        for (auto& p : m_outbound_peers) {
            if (p == nullptr) {
                p = std::move(p_peer);
                break;
            }
        }
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

    LOG_INFO("Disconnected peer " + p_peer->peer_node->GetAddress());
}

/**
 * @brief Remove disconnected peers from both inbound and outbound lists
 *
 * Thread-safe cleanup using erase-remove idiom:
 * 1. Acquires mutex locks
 * 2. Finds all peers where f_connected == false
 * 3. For inbound peers: Cancel async operations and close descriptors
 * 4. Removes them from both peer vectors
 *
 * Called periodically by PeerManagerThread() every 5 seconds.
 * For outbound peers, assumes connection threads have already been joined.
 */
void CPeerManager::CleanupDisconnectedPeers() {
    // First, cancel and close async descriptors for disconnected inbound peers
    {
        std::lock_guard<std::mutex> lock_peers(cs_peers);
        std::lock_guard<std::mutex> lock_descriptors(cs_inbound_descriptors);

        for (const auto& p_peer : m_inbound_peers) {
            if (p_peer && !p_peer->f_connected && p_peer->n_socket >= 0) {
                // Find and cleanup the descriptor
                auto it = map_inbound_descriptors.find(p_peer->n_socket);
                if (it != map_inbound_descriptors.end()) {
                    auto& p_descriptor = it->second;
                    if (p_descriptor) {
                        // Cancel pending async operations
                        boost::system::error_code cancel_ec;
                        p_descriptor->cancel(cancel_ec);

                        // Close the descriptor
                        boost::system::error_code close_ec;
                        p_descriptor->close(close_ec);

                        LOG_TRACE("Cleaned up async descriptor for socket " + std::to_string(p_peer->n_socket));
                    }
                    map_inbound_descriptors.erase(it);
                }
            }
        }
    }

    // Then remove disconnected peers from both lists
    {
        std::lock_guard<std::mutex> lock(cs_peers);

        // Remove disconnected inbound peers
        size_t inbound_before = m_inbound_peers.size();
        m_inbound_peers.erase(
            std::remove_if(m_inbound_peers.begin(), m_inbound_peers.end(),
                [](const std::unique_ptr<CPeerConnection>& p_peer) {
                    return p_peer && !p_peer->f_connected;
                }),
            m_inbound_peers.end()
        );
        size_t inbound_removed = inbound_before - m_inbound_peers.size();
        if (inbound_removed > 0) {
            LOG_INFO("Cleaned up " + std::to_string(inbound_removed) + " disconnected inbound peer(s)");
        }

        // Remove disconnected outbound peers
        size_t outbound_before = m_outbound_peers.size();
        m_outbound_peers.erase(
            std::remove_if(m_outbound_peers.begin(), m_outbound_peers.end(),
                [](const std::unique_ptr<CPeerConnection>& p_peer) {
                    return p_peer && !p_peer->f_connected;
                }),
            m_outbound_peers.end()
        );
        size_t outbound_removed = outbound_before - m_outbound_peers.size();
        if (outbound_removed > 0) {
            LOG_INFO("Cleaned up " + std::to_string(outbound_removed) + " disconnected outbound peer(s)");
        }
    }
}

/**
 * @brief Add outbound connection to peer
 * @param str_address Peer IP address or hostname
 * @param n_port Peer listening port
 * @return true if connection initiated successfully, false on error
 *
 * Validation checks (thread-safe with mutex):
 * 1. Verify we haven't reached max outbound peers limit
 * 2. Check if already connected to this address:port
 *
 * If validation passes, delegates to ConnectToPeer() for actual
 * connection. Connection happens synchronously but message handling
 * occurs asynchronously in background thread.
 */
bool CPeerManager::AddPeer(const std::string& str_address, int n_port) {
    // Check if we've reached max outbound peers and reserve a slot
    // We need to do this atomically to prevent race conditions where
    // multiple threads could pass the size check simultaneously
    {
        std::lock_guard<std::mutex> lock(cs_peers);
        if (m_outbound_peers.size() >= static_cast<size_t>(n_max_outbound_peers)) {
            LOG_WARN("Maximum outbound peers reached (" + std::to_string(n_max_outbound_peers) + ")");
            return false;
        }

        // Check if already connected to this peer
        for (const auto& p_peer : m_outbound_peers) {
            if (p_peer && p_peer->peer_node->GetAddress() == str_address && p_peer->peer_node->GetPort() == n_port) {
                LOG_INFO("Already connected to peer " + str_address + ":" + std::to_string(n_port));
                return false;
            }
        }

        // Reserve a slot by adding a placeholder (nullptr)
        // This prevents other threads from exceeding n_max_outbound_peers
        // while we perform the blocking connection operation
        m_outbound_peers.push_back(nullptr);
    }

    // Perform blocking connection outside the lock
    bool f_success = ConnectToPeer(str_address, n_port);

    if (!f_success) {
        // Connection failed, remove the placeholder we added
        std::lock_guard<std::mutex> lock(cs_peers);
        // Remove the last nullptr entry (our placeholder)
        m_outbound_peers.erase(
            std::remove_if(m_outbound_peers.begin(), m_outbound_peers.end(),
                [](const std::unique_ptr<CPeerConnection>& p) { return p == nullptr; }),
            m_outbound_peers.end()
        );
    }

    return f_success;
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
    size_t n_count = 0;
    for (const auto& p_peer : m_outbound_peers) {
        if (p_peer && p_peer->f_connected) {
            n_count++;
        }
    }
    return n_count;
}

/**
 * @brief Get count of active inbound peer connections
 * @return Number of inbound peers
 *
 * Thread-safe count using mutex lock. Includes disconnected peers
 * that haven't been cleaned up yet by CleanupDisconnectedPeers().
 */
size_t CPeerManager::GetInboundPeerCount() const {
    std::lock_guard<std::mutex> lock(cs_peers);
    size_t n_count = 0;
    for (const auto& p_peer : m_inbound_peers) {
        if (p_peer && p_peer->f_connected) {
            n_count++;
        }
    }
    return n_count;
}

/**
 * @brief Get list of connected peer addresses (both inbound and outbound)
 * @return Vector of "address:port" strings for connected peers
 *
 * Thread-safe snapshot of currently connected peers.
 * Filters out disconnected peers (f_connected == false).
 * Returns empty vector if no peers connected.
 */
std::vector<CPeerNode> CPeerManager::GetConnectedPeers() const {
    std::vector<CPeerNode> peers;
    std::lock_guard<std::mutex> lock(cs_peers);

    // Add inbound peers
    for (const auto& p_peer : m_inbound_peers) {
        if (p_peer && p_peer->f_connected) {
            peers.push_back(*p_peer->peer_node);
        }
    }

    // Add outbound peers
    for (const auto& p_peer : m_outbound_peers) {
        if (p_peer && p_peer->f_connected) {
            peers.push_back(*p_peer->peer_node);
        }
    }

    return peers;
}

/**
 * @brief Send a message to a specific peer
 * @param str_address Peer IP address or hostname
 * @param n_port Peer listening port
 * @param message The peer message to send
 * @return true if message was sent successfully, false on error
 *
 * Finds the peer by address and port, serializes the message, and sends it
 * over the TCP connection. Returns false if peer not found or send fails.
 */
bool CPeerManager::SendMessageToPeer(const std::string& str_address, int n_port, const CPeerMessage& message) {
    // Serialize the message
    std::string str_serialized = message.Serialize();
    if (str_serialized.empty()) {
        LOG_ERROR("Failed to serialize message of type " + message.GetTypeString());
        return false;
    }

    // Find the peer and get socket while holding the lock
    int n_target_socket = -1;
    std::string str_peer_id;

    {
        std::lock_guard<std::mutex> lock(cs_peers);

        // Search in outbound peers
        for (const auto& p_peer : m_outbound_peers) {
            if (p_peer && p_peer->peer_node->GetAddress() == str_address && p_peer->peer_node->GetPort() == n_port) {
                if (p_peer->f_connected && p_peer->n_socket >= 0) {
                    n_target_socket = p_peer->n_socket;
                    str_peer_id = p_peer->peer_node->GetAddress() + ":" + std::to_string(p_peer->peer_node->GetPort());
                }
                break;
            }
        }

        // If not found in outbound, search in inbound peers
        if (n_target_socket == -1) {
            for (const auto& p_peer : m_inbound_peers) {
                if (p_peer && p_peer->peer_node->GetAddress() == str_address && p_peer->peer_node->GetPort() == n_port) {
                    if (p_peer->f_connected && p_peer->n_socket >= 0) {
                        n_target_socket = p_peer->n_socket;
                        str_peer_id = p_peer->peer_node->GetAddress() + ":" + std::to_string(p_peer->peer_node->GetPort());
                    }
                    break;
                }
            }
        }
    }

    // Check if peer was found
    if (n_target_socket == -1) {
        LOG_WARN("Cannot send message to " + str_address + ":" + std::to_string(n_port) + " - peer not connected");
        return false;
    }

    // Send the serialized message without holding the lock
    ssize_t n_sent = send(n_target_socket, str_serialized.c_str(), str_serialized.length(), 0);

    if (n_sent > 0) {
        LOG_TRACE("Sent " + message.GetTypeString() + " message (" + std::to_string(str_serialized.length()) +
                  " bytes) to peer " + str_peer_id);
        return true;
    } else {
        LOG_ERROR("Failed to send " + message.GetTypeString() + " message to peer " + str_peer_id +
                  " (error: " + std::string(strerror(errno)) + ")");
        return false;
    }
}

/**
 * @brief Broadcast a message to all connected peers
 * @param message The peer message to broadcast
 * @return Number of peers the message was successfully sent to
 *
 * Serializes the message once and sends it to all connected peers
 * (both inbound and outbound). Failed sends are logged but don't prevent
 * sends to other peers.
 */
size_t CPeerManager::BroadcastMessage(const CPeerMessage& message) {
    // Serialize the message once
    std::string str_serialized = message.Serialize();
    if (str_serialized.empty()) {
        LOG_ERROR("Failed to serialize message of type " + message.GetTypeString() + " for broadcast");
        return 0;
    }

    // Copy socket descriptors and peer info while holding the lock
    // This prevents blocking I/O operations from holding the mutex
    struct PeerInfo {
        int n_socket;
        std::string str_peer_address;
    };
    std::vector<PeerInfo> peer_sockets;

    {
        std::lock_guard<std::mutex> lock(cs_peers);

        // Add all connected outbound peers
        for (const auto& p_peer : m_outbound_peers) {
            if (p_peer && p_peer->f_connected && p_peer->n_socket >= 0) {
                peer_sockets.push_back({
                    p_peer->n_socket,
                    p_peer->peer_node->GetAddress() + ":" + std::to_string(p_peer->peer_node->GetPort())
                });
            }
        }

        // Add all connected inbound peers
        for (const auto& p_peer : m_inbound_peers) {
            if (p_peer && p_peer->f_connected && p_peer->n_socket >= 0) {
                peer_sockets.push_back({
                    p_peer->n_socket,
                    p_peer->peer_node->GetAddress() + ":" + std::to_string(p_peer->peer_node->GetPort())
                });
            }
        }
    }

    // Log detailed broadcast information at TRACE level
    if (!peer_sockets.empty()) {
        std::string str_peer_list;
        for (size_t i = 0; i < peer_sockets.size(); ++i) {
            if (i > 0) str_peer_list += ", ";
            str_peer_list += peer_sockets[i].str_peer_address;
        }
        LOG_TRACE("Broadcasting " + message.GetTypeString() + " message (" +
                 std::to_string(str_serialized.length()) + " bytes) to " +
                 std::to_string(peer_sockets.size()) + " peers: " + str_peer_list);
    } else {
        LOG_TRACE("Broadcasting " + message.GetTypeString() + " message (" +
                 std::to_string(str_serialized.length()) + " bytes) to 0 peers");
    }

    // Send to all peers without holding the lock
    // This prevents slow/blocked sockets from blocking other operations
    size_t n_sent_count = 0;

    for (const auto& peer_info : peer_sockets) {
        ssize_t n_sent = send(peer_info.n_socket, str_serialized.c_str(), str_serialized.length(), 0);
        if (n_sent > 0) {
            n_sent_count++;
            LOG_TRACE("Successfully sent " + message.GetTypeString() + " to peer " + peer_info.str_peer_address);
        } else {
            LOG_ERROR("Failed to send " + message.GetTypeString() + " to peer " + peer_info.str_peer_address +
                      " (error: " + std::string(strerror(errno)) + ")");
        }
    }

    LOG_TRACE("Broadcast complete: sent " + message.GetTypeString() + " to " + std::to_string(n_sent_count) + " of " +
             std::to_string(peer_sockets.size()) + " peers");

    return n_sent_count;
}

/**
 * @brief Send PING message to all connected peers immediately
 * @return Number of peers the PING was successfully sent to
 *
 * Sends PING messages with unique nonce to each connected peer.
 * This is similar to the periodic PING in PeerThread but can be triggered on-demand.
 */
size_t CPeerManager::SendPingToAllPeers() {
    LOG_INFO("SendPingToAllPeers: Sending PING to all connected peers");

    size_t n_sent = 0;
    auto ping_send_time = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(cs_peers);

        // Send PING to outbound peers (synchronous send in OutboundConnectionThread)
        for (auto& p_peer : m_outbound_peers) {
            if (p_peer && p_peer->f_connected && p_peer->n_socket >= 0) {
                // Generate random nonce
                uint32_t n_nonce = static_cast<uint32_t>(rand());

                // Create PING message with nonce as payload
                std::vector<uint8_t> nonce_bytes(4);
                nonce_bytes[0] = (n_nonce >> 24) & 0xFF;
                nonce_bytes[1] = (n_nonce >> 16) & 0xFF;
                nonce_bytes[2] = (n_nonce >> 8) & 0xFF;
                nonce_bytes[3] = n_nonce & 0xFF;
                CPeerMessage ping_message(MessageType::PING, nonce_bytes);

                // Store nonce and send time for verification when PONG arrives
                p_peer->n_last_ping_nonce = n_nonce;
                p_peer->m_last_ping_send_time = ping_send_time;

                // Serialize and send
                std::string str_serialized = ping_message.Serialize();
                ssize_t n_bytes_sent = send(p_peer->n_socket, str_serialized.c_str(), str_serialized.length(), 0);
                if (n_bytes_sent > 0) {
                    n_sent++;
                    LOG_TRACE("Sent " + ping_message.GetType() + " with nonce " + std::to_string(n_nonce) + " to peer " +
                             p_peer->peer_node->GetIdentifier());
                }
            }
        }

        // Send PING to inbound peers (using async I/O)
        for (auto& p_peer : m_inbound_peers) {
            if (p_peer && p_peer->f_connected && p_peer->n_socket >= 0) {
                // Generate random nonce
                uint32_t n_nonce = static_cast<uint32_t>(rand());

                // Create PING message with nonce as payload
                std::vector<uint8_t> nonce_bytes(4);
                nonce_bytes[0] = (n_nonce >> 24) & 0xFF;
                nonce_bytes[1] = (n_nonce >> 16) & 0xFF;
                nonce_bytes[2] = (n_nonce >> 8) & 0xFF;
                nonce_bytes[3] = n_nonce & 0xFF;
                CPeerMessage ping_message(MessageType::PING, nonce_bytes);

                // Store nonce and send time for verification when PONG arrives
                p_peer->n_last_ping_nonce = n_nonce;
                p_peer->m_last_ping_send_time = ping_send_time;

                // Send via async I/O (for inbound peers)
                SendMessageAsync(p_peer->n_socket, ping_message);
                n_sent++;
                LOG_TRACE("Queued async " + ping_message.GetType() + " with nonce " + std::to_string(n_nonce) + " to inbound peer " +
                         p_peer->peer_node->GetIdentifier());
            }
        }
    }

    LOG_INFO("Sent " + MessageType::PING + " to " + std::to_string(n_sent) + " peers");
    return n_sent;
}

void CPeerManager::MarkInventoryKnown(std::shared_ptr<IPeerNode> p_peer_node, ObjectType::Type obj_type, const std::string& str_inventory_hash) {
    if (!p_peer_node) {
        return;  // Ignore null peer nodes
    }

    std::lock_guard<std::mutex> lock(cs_inventory);
    map_inventory_known[p_peer_node][obj_type].insert(str_inventory_hash);
}

bool CPeerManager::PeerKnowsInventory(std::shared_ptr<IPeerNode> p_peer_node, ObjectType::Type obj_type, const std::string& str_inventory_hash) {
    if (!p_peer_node) {
        return false;  // Null peer knows nothing
    }

    std::lock_guard<std::mutex> lock(cs_inventory);
    auto it_peer = map_inventory_known.find(p_peer_node);
    if (it_peer == map_inventory_known.end()) {
        return false;  // Peer not in map
    }

    auto it_type = it_peer->second.find(obj_type);
    if (it_type == it_peer->second.end()) {
        return false;  // Object type not tracked for this peer
    }

    return it_type->second.find(str_inventory_hash) != it_type->second.end();
}

void CPeerManager::BroadcastInventory(const std::vector<std::pair<ObjectType::Type, std::string>>& vec_inventory) {
    if (vec_inventory.empty()) {
        return;  // Nothing to broadcast
    }

    std::lock_guard<std::mutex> lock(cs_peers);
    size_t n_total_sent = 0;

    // Build inventory per peer (filtering items they already know)
    std::vector<CPeerConnection*> all_peers;
    for (auto& p_peer : m_inbound_peers) {
        if (p_peer && p_peer->f_connected) {
            all_peers.push_back(p_peer.get());
        }
    }
    for (auto& p_peer : m_outbound_peers) {
        if (p_peer && p_peer->f_connected) {
            all_peers.push_back(p_peer.get());
        }
    }

    // For each peer, build custom inventory message with items they don't know
    for (CPeerConnection* p_peer : all_peers) {
        // Filter inventory to items this peer doesn't know about
        std::vector<std::pair<ObjectType::Type, std::string>> peer_inventory;

        for (const auto& item : vec_inventory) {
            if (!PeerKnowsInventory(p_peer->peer_node, item.first, item.second)) {
                peer_inventory.push_back(item);
            }
        }

        if (peer_inventory.empty()) {
            continue;  // Peer already knows everything
        }

        // Build message: [count][type][hash][type][hash]...
        std::string str_payload;

        // Write count (MESSAGE_COUNT_SIZE bytes, network byte order)
        uint32_t n_count = static_cast<uint32_t>(peer_inventory.size());
        uint32_t n_count_network = htonl(n_count);
        str_payload.append(reinterpret_cast<const char*>(&n_count_network), MESSAGE_COUNT_SIZE);

        // Write each item (type + hash)
        for (const auto& item : peer_inventory) {
            // Write type (MESSAGE_TYPE_SIZE bytes)
            char type_buf[MESSAGE_TYPE_SIZE];
            WriteObjectType(item.first, type_buf);
            str_payload.append(type_buf, MESSAGE_TYPE_SIZE);

            // Write hash (MESSAGE_HASH_SIZE bytes, binary format)
            str_payload.append(item.second);
        }

        // Send message
        CPeerMessage inv_message(MessageType::INVENTORY, str_payload);
        SendMessageAsync(p_peer->n_socket, inv_message);

        // Mark all items as known by this peer
        for (const auto& item : peer_inventory) {
            MarkInventoryKnown(p_peer->peer_node, item.first, item.second);
        }

        n_total_sent++;
    }

    LOG_INFO("Broadcasted INVENTORY with " + std::to_string(vec_inventory.size()) +
             " items to " + std::to_string(n_total_sent) + " peers");
}

void CPeerManager::ScheduleGetDataMessage(int n_socket, const std::vector<std::pair<ObjectType::Type, std::string>>& vec_items) {
    if (vec_items.empty()) {
        return;  // Nothing to request
    }

    // Build GETDATA message: [count][type][hash][type][hash]...
    std::string str_payload;

    // Write count (MESSAGE_COUNT_SIZE bytes, network byte order)
    uint32_t n_count = static_cast<uint32_t>(vec_items.size());
    uint32_t n_count_network = htonl(n_count);
    str_payload.append(reinterpret_cast<const char*>(&n_count_network), MESSAGE_COUNT_SIZE);

    // Write each item (type + hash)
    for (const auto& item : vec_items) {
        // Write type (MESSAGE_TYPE_SIZE bytes)
        char type_buf[MESSAGE_TYPE_SIZE];
        WriteObjectType(item.first, type_buf);
        str_payload.append(type_buf, MESSAGE_TYPE_SIZE);

        // Write hash (MESSAGE_HASH_SIZE bytes, binary format)
        str_payload.append(item.second);
    }

    // Send message
    CPeerMessage getdata_message(MessageType::GETDATA, str_payload);
    SendMessageAsync(n_socket, getdata_message);

    LOG_TRACE("Sent GETDATA request for " + std::to_string(vec_items.size()) + " items");
}

// ============= Connection Rotation =============

void CPeerManager::RotateOutboundConnections() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_last_rotation_time).count();

    if (elapsed < PEER_ROTATION_INTERVAL) {
        return;  // Not time to rotate yet
    }

    std::lock_guard<std::mutex> lock(cs_peers);

    if (m_outbound_peers.size() < 2) {
        return;  // Need at least 2 peers to rotate
    }

    // Find oldest 1-2 connections based on connection time
    std::vector<CPeerConnection*> oldest_peers;
    for (auto& p_peer : m_outbound_peers) {
        if (p_peer && p_peer->f_connected) {
            oldest_peers.push_back(p_peer.get());
        }
    }

    // Sort by connection time (oldest first)
    std::sort(oldest_peers.begin(), oldest_peers.end(),
              [](CPeerConnection* a, CPeerConnection* b) {
                  return a->peer_node->GetConnectionTime() < b->peer_node->GetConnectionTime();
              });

    // Disconnect 1-2 oldest connections
    int n_to_disconnect = std::min(2, static_cast<int>(oldest_peers.size()));
    for (int i = 0; i < n_to_disconnect; i++) {
        LOG_INFO("Rotating out outbound peer: " + oldest_peers[i]->peer_node->GetAddress());
        DisconnectPeer(oldest_peers[i]);
    }

    m_last_rotation_time = now;
    LOG_INFO("Rotated " + std::to_string(n_to_disconnect) + " outbound connections");
}

// ============= Subnet-Based Inbound Rotation =============

std::string CPeerManager::GetSubnet(const std::string& str_address) {
    // Extract /24 subnet (first 3 octets) for IPv4
    size_t n_third_dot = str_address.find_last_of('.');
    if (n_third_dot != std::string::npos) {
        return str_address.substr(0, n_third_dot);
    }
    return str_address;  // Return full address if parsing fails
}

void CPeerManager::EnforceSubnetDiversity() {
    std::lock_guard<std::mutex> lock(cs_peers);

    // Count connections per subnet
    std::map<std::string, std::vector<CPeerConnection*>> subnet_connections;

    for (auto& p_peer : m_inbound_peers) {
        if (p_peer && p_peer->f_connected) {
            std::string str_subnet = GetSubnet(p_peer->peer_node->GetAddress());
            subnet_connections[str_subnet].push_back(p_peer.get());
        }
    }

    // Disconnect one oldest connection from each subnet that has multiple connections
    // Since there is one addpeer request, just remove one oldest peer from the same subnet
    int n_disconnected = 0;

    for (auto& pair : subnet_connections) {
        if (pair.second.size() > 1) {
            // Sort by connection time (oldest first)
            std::sort(pair.second.begin(), pair.second.end(),
                     [](CPeerConnection* a, CPeerConnection* b) {
                         return a->peer_node->GetConnectionTime() < b->peer_node->GetConnectionTime();
                     });

            // Disconnect only the oldest connection from this subnet
            LOG_INFO("Enforcing subnet diversity: disconnecting oldest peer from subnet " +
                     pair.first + ": " + pair.second[0]->peer_node->GetAddress());
            DisconnectPeer(pair.second[0]);
            n_disconnected++;
        }
    }

    if (n_disconnected > 0) {
        LOG_INFO("Enforced subnet diversity: disconnected " + std::to_string(n_disconnected) + " peers");
    }
}

// ============= Malicious Peer Detection and Banning =============

void CPeerManager::IncreaseMisbehaviorScore(const std::string& str_peer_address, int n_score_increase) {
    std::lock_guard<std::mutex> lock(cs_bans);

    map_peer_misbehavior[str_peer_address] += n_score_increase;
    int n_total_score = map_peer_misbehavior[str_peer_address];

    LOG_WARN("Peer " + str_peer_address + " misbehavior score increased by " +
             std::to_string(n_score_increase) + " to " + std::to_string(n_total_score));

    // Ban peer if score exceeds threshold
    if (n_total_score >= PEER_BAN_THRESHOLD) {
        auto ban_expiry = std::chrono::steady_clock::now() + std::chrono::seconds(PEER_BAN_DURATION);
        map_banned_peers[str_peer_address] = ban_expiry;

        LOG_ERROR("Peer " + str_peer_address + " banned for " + std::to_string(PEER_BAN_DURATION) +
                 " seconds (score: " + std::to_string(n_total_score) + ")");

        // Disconnect peer
        std::lock_guard<std::mutex> peer_lock(cs_peers);
        for (auto& p_peer : m_inbound_peers) {
            if (p_peer && p_peer->peer_node->GetAddress() == str_peer_address) {
                DisconnectPeer(p_peer.get());
                break;
            }
        }
        for (auto& p_peer : m_outbound_peers) {
            if (p_peer && p_peer->peer_node->GetAddress() == str_peer_address) {
                DisconnectPeer(p_peer.get());
                break;
            }
        }
    }
}

bool CPeerManager::IsPeerBanned(const std::string& str_peer_address) {
    std::lock_guard<std::mutex> lock(cs_bans);

    auto it = map_banned_peers.find(str_peer_address);
    if (it == map_banned_peers.end()) {
        return false;
    }

    // Check if ban has expired
    auto now = std::chrono::steady_clock::now();
    if (now >= it->second) {
        // Ban expired
        map_banned_peers.erase(it);
        map_peer_misbehavior.erase(str_peer_address);
        LOG_INFO("Ban expired for peer " + str_peer_address);
        return false;
    }

    return true;
}

void CPeerManager::CleanupExpiredBans() {
    std::lock_guard<std::mutex> lock(cs_bans);

    auto now = std::chrono::steady_clock::now();
    auto it = map_banned_peers.begin();

    while (it != map_banned_peers.end()) {
        if (now >= it->second) {
            LOG_INFO("Removing expired ban for peer " + it->first);
            map_peer_misbehavior.erase(it->first);
            it = map_banned_peers.erase(it);
        } else {
            ++it;
        }
    }
}

// ============= Blockweave Integration =============

void CPeerManager::SetBlockweave(std::shared_ptr<IBlockweave> p_bw) {
    p_blockweave = p_bw;
    if (p_blockweave) {
        LOG_INFO("Blockweave instance set for peer manager (shared ownership)");
    } else {
        LOG_INFO("Blockweave instance cleared from peer manager");
    }
}
