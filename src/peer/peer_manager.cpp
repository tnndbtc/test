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
#include "logger/logger.h"
#include <iostream>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <algorithm>
#include <sstream>

// ============= CPeerConnection Implementation =============

/**
 * @brief Default constructor - creates disconnected peer
 *
 * Initializes all fields to default values (no socket, no connection).
 */
CPeerConnection::CPeerConnection()
    : n_socket(-1), peer_node(), f_connected(false), f_active(false) {}

/**
 * @brief Construct peer with address and port
 * @param str_addr Peer IP address or hostname
 * @param n_port_num Peer listening port
 *
 * Creates peer connection object but does not establish connection.
 * Call ConnectToPeer() to actually connect.
 */
CPeerConnection::CPeerConnection(const std::string& str_addr, int n_port_num)
    : n_socket(-1), peer_node(str_addr, n_port_num), f_connected(false), f_active(false) {}

/**
 * @brief Construct peer with CPeerNode
 * @param node Peer node information
 *
 * Creates peer connection object from existing CPeerNode.
 * Does not establish connection - call ConnectToPeer() to actually connect.
 */
CPeerConnection::CPeerConnection(const CPeerNode& node)
    : n_socket(-1), peer_node(node), f_connected(false), f_active(false) {}

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
      peer_node(std::move(other.peer_node)),
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
        peer_node = std::move(other.peer_node);
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
 * @brief Construct peer manager with listening port and max peers
 * @param n_port Port to listen on for inbound connections
 * @param n_max_outbound Maximum number of outbound peer connections
 * @param n_max_inbound Maximum number of inbound peer connections
 *
 * Initializes peer manager in stopped state. Reserves space for
 * peer connections to avoid vector reallocations during operation.
 * Call Start() to begin accepting connections.
 */
CPeerManager::CPeerManager(int n_port, int n_max_outbound, int n_max_inbound)
    : n_listen_port(n_port), n_listen_socket(-1),
      n_max_inbound_peers(n_max_inbound), n_max_outbound_peers(n_max_outbound),
      f_running(false), f_stop_requested(false),
      m_last_ping_time(std::chrono::steady_clock::now()) {
    m_inbound_peers.reserve(n_max_inbound_peers);
    m_outbound_peers.reserve(n_max_outbound_peers);
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
    LOG_TRACE("Maximum inbound peers: " + std::to_string(n_max_inbound_peers));
    LOG_TRACE("Maximum outbound peers: " + std::to_string(n_max_outbound_peers));

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

    // Stop all peer connections (both inbound and outbound)
    {
        std::lock_guard<std::mutex> lock(cs_peers);
        // Stop inbound peers
        for (auto& p_peer : m_inbound_peers) {
            if (p_peer) {
                p_peer->f_active = false;
                if (p_peer->n_socket >= 0) {
                    shutdown(p_peer->n_socket, SHUT_RDWR);
                    close(p_peer->n_socket);
                    p_peer->n_socket = -1;
                }
            }
        }
        // Stop outbound peers
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
        // Join inbound peer threads
        for (auto& p_peer : m_inbound_peers) {
            if (p_peer && p_peer->m_thread.joinable()) {
                p_peer->m_thread.join();
            }
        }
        // Join outbound peer threads
        for (auto& p_peer : m_outbound_peers) {
            if (p_peer && p_peer->m_thread.joinable()) {
                p_peer->m_thread.join();
            }
        }
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
 * - Sends PING messages to all connected peers every 30 seconds
 * - Runs every 5 seconds while peer manager is active
 *
 * Exits when f_stop_requested is set by Stop().
 */
void CPeerManager::PeerThread() {
    // Set thread name for easier debugging and logging
    SetThreadName("peer_manager");

    LOG_INFO("Peer management thread started");

    while (!f_stop_requested) {
        // Clean up disconnected peers
        CleanupDisconnectedPeers();

        // Check if it's time to send PING messages (every 30 seconds)
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_last_ping_time);

        if (elapsed.count() >= 30) {
            // Send PING to all connected peers
            size_t n_peer_count = GetOutboundPeerCount() + GetInboundPeerCount();
            if (n_peer_count > 0) {
                CPeerMessage ping_message("ping");
                size_t n_sent = BroadcastMessage(ping_message);
                LOG_INFO("Sent PING to " + std::to_string(n_sent) + " peers");
            }
            m_last_ping_time = now;
        }

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

        // Check if we've reached max inbound peers
        {
            std::lock_guard<std::mutex> lock(cs_peers);
            if (m_inbound_peers.size() >= static_cast<size_t>(n_max_inbound_peers)) {
                LOG_WARN("Maximum inbound peers reached (" + std::to_string(n_max_inbound_peers) +
                         "), rejecting connection from " + std::string(str_ip) + ":" + std::to_string(n_peer_port));
                close(n_client_socket);
                continue;
            }

            LOG_INFO("Accepted inbound peer connection from " + std::string(str_ip) + ":" + std::to_string(n_peer_port));

            // Set socket keepalive
            SetSocketKeepAlive(n_client_socket);

            // Create peer connection object for inbound peer
            auto p_peer = std::make_unique<CPeerConnection>(std::string(str_ip), n_peer_port);
            p_peer->n_socket = n_client_socket;
            p_peer->f_active = true;
            p_peer->f_connected = true;

            // Start connection thread for this peer
            p_peer->m_thread = std::thread(&CPeerManager::ConnectionThread, this, p_peer.get());

            // Add to inbound peers list
            m_inbound_peers.push_back(std::move(p_peer));
        }
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

    // Set thread name for easier debugging and logging
    // Use format: peer_<address>
    std::ostringstream oss;
    oss << "peer_" << p_peer->peer_node.GetAddress();
    SetThreadName(oss.str());

    LOG_INFO("Connection thread started for peer " + p_peer->peer_node.GetAddress() + ":" + std::to_string(p_peer->peer_node.GetPort()));

    // Set socket to non-blocking mode for receiving
    SetSocketNonBlocking(p_peer->n_socket, true);

    // Connection keep-alive loop with message handling
    char buffer[4096];
    std::string str_receive_buffer;

    while (p_peer->f_active && !f_stop_requested) {
        // Check if socket is still connected
        if (p_peer->n_socket < 0) {
            LOG_WARN("Peer socket closed: " + p_peer->peer_node.GetAddress());
            break;
        }

        // Try to receive data (non-blocking)
        ssize_t n_bytes_received = recv(p_peer->n_socket, buffer, sizeof(buffer) - 1, 0);

        if (n_bytes_received > 0) {
            str_receive_buffer += std::string(buffer, n_bytes_received);
            LOG_TRACE("Received " + std::to_string(n_bytes_received) + " bytes from peer " + p_peer->peer_node.GetAddress());

            // Try to deserialize complete messages from buffer
            // CPeerMessage format: [1 byte type_length][N bytes type][4 bytes payload_length][M bytes payload]
            while (str_receive_buffer.size() >= CPeerMessage::GetMinHeaderSize()) {
                // Try to deserialize a message
                CPeerMessage received_msg;
                if (received_msg.Deserialize(str_receive_buffer)) {
                    // Successfully deserialized a message
                    std::string msg_type = received_msg.GetType();
                    LOG_TRACE("Received " + msg_type + " message from peer " + p_peer->peer_node.GetAddress());

                    // Calculate message size and remove from buffer
                    // Message size = 1 (type_length) + type_length + 4 (payload_length) + payload_length
                    size_t type_len = static_cast<uint8_t>(str_receive_buffer[0]);
                    size_t msg_size = 1 + type_len + 4 + received_msg.GetPayloadSize();
                    str_receive_buffer.erase(0, msg_size);

                    // Handle different message types
                    if (msg_type == MessageType::PING) {
                        // Immediately respond with PONG
                        LOG_INFO("Received PING from peer " + p_peer->peer_node.GetAddress() + ", sending PONG");
                        CPeerMessage pong_msg(MessageType::PONG);

                        // Send PONG directly through socket (we're already in the connection thread)
                        std::string str_serialized = pong_msg.Serialize();
                        ssize_t n_sent = send(p_peer->n_socket, str_serialized.c_str(), str_serialized.length(), 0);

                        if (n_sent > 0) {
                            LOG_TRACE("Sent PONG response to peer " + p_peer->peer_node.GetAddress());
                        } else {
                            LOG_ERROR("Failed to send PONG to peer " + p_peer->peer_node.GetAddress() +
                                     " (error: " + std::string(strerror(errno)) + ")");
                        }
                    } else if (msg_type == MessageType::PONG) {
                        LOG_TRACE("Received PONG from peer " + p_peer->peer_node.GetAddress());
                    } else if (msg_type == MessageType::TX_IDS) {
                        std::string str_tx_ids = received_msg.GetPayloadString();
                        LOG_INFO("Received transaction IDs from peer " + p_peer->peer_node.GetAddress() + ": " + str_tx_ids);
                        // TODO: Process transaction IDs (e.g., request full transactions if not in mempool)
                    } else {
                        LOG_TRACE("Received unknown message type '" + msg_type + "' from peer " + p_peer->peer_node.GetAddress());
                    }
                } else {
                    // Not enough data yet for a complete message, wait for more
                    break;
                }
            }
        } else if (n_bytes_received == 0) {
            // Connection closed by peer
            LOG_INFO("Peer closed connection: " + p_peer->peer_node.GetAddress());
            break;
        } else {
            // Check for errors (EAGAIN/EWOULDBLOCK is normal for non-blocking sockets)
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG_ERROR("Error receiving from peer " + p_peer->peer_node.GetAddress() + ": " + std::string(strerror(errno)));
                break;
            }
        }

        // Sleep briefly to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_INFO("Connection thread stopped for peer " + p_peer->peer_node.GetAddress());
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

    LOG_INFO("Disconnected peer " + p_peer->peer_node.GetAddress());
}

/**
 * @brief Remove disconnected peers from both inbound and outbound lists
 *
 * Thread-safe cleanup using erase-remove idiom:
 * 1. Acquires mutex lock
 * 2. Finds all peers where f_connected == false
 * 3. Removes them from both peer vectors
 *
 * Called periodically by PeerThread() every 5 seconds.
 * Assumes peer connection threads have already been joined.
 */
void CPeerManager::CleanupDisconnectedPeers() {
    std::lock_guard<std::mutex> lock(cs_peers);

    // Remove disconnected inbound peers
    m_inbound_peers.erase(
        std::remove_if(m_inbound_peers.begin(), m_inbound_peers.end(),
            [](const std::unique_ptr<CPeerConnection>& p_peer) {
                return p_peer && !p_peer->f_connected;
            }),
        m_inbound_peers.end()
    );

    // Remove disconnected outbound peers
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
            if (p_peer && p_peer->peer_node.GetAddress() == str_address && p_peer->peer_node.GetPort() == n_port) {
                LOG_WARN("Already connected to peer " + str_address + ":" + std::to_string(n_port));
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
    return m_outbound_peers.size();
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
    return m_inbound_peers.size();
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
            peers.push_back(p_peer->peer_node);
        }
    }

    // Add outbound peers
    for (const auto& p_peer : m_outbound_peers) {
        if (p_peer && p_peer->f_connected) {
            peers.push_back(p_peer->peer_node);
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
            if (p_peer && p_peer->peer_node.GetAddress() == str_address && p_peer->peer_node.GetPort() == n_port) {
                if (p_peer->f_connected && p_peer->n_socket >= 0) {
                    n_target_socket = p_peer->n_socket;
                    str_peer_id = p_peer->peer_node.GetAddress() + ":" + std::to_string(p_peer->peer_node.GetPort());
                }
                break;
            }
        }

        // If not found in outbound, search in inbound peers
        if (n_target_socket == -1) {
            for (const auto& p_peer : m_inbound_peers) {
                if (p_peer && p_peer->peer_node.GetAddress() == str_address && p_peer->peer_node.GetPort() == n_port) {
                    if (p_peer->f_connected && p_peer->n_socket >= 0) {
                        n_target_socket = p_peer->n_socket;
                        str_peer_id = p_peer->peer_node.GetAddress() + ":" + std::to_string(p_peer->peer_node.GetPort());
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

    LOG_INFO("Broadcasting " + message.GetTypeString() + " message (" +
             std::to_string(str_serialized.length()) + " bytes) to all peers");

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
                    p_peer->peer_node.GetAddress() + ":" + std::to_string(p_peer->peer_node.GetPort())
                });
            }
        }

        // Add all connected inbound peers
        for (const auto& p_peer : m_inbound_peers) {
            if (p_peer && p_peer->f_connected && p_peer->n_socket >= 0) {
                peer_sockets.push_back({
                    p_peer->n_socket,
                    p_peer->peer_node.GetAddress() + ":" + std::to_string(p_peer->peer_node.GetPort())
                });
            }
        }
    }

    // Send to all peers without holding the lock
    // This prevents slow/blocked sockets from blocking other operations
    size_t n_sent_count = 0;

    for (const auto& peer_info : peer_sockets) {
        ssize_t n_sent = send(peer_info.n_socket, str_serialized.c_str(), str_serialized.length(), 0);
        if (n_sent > 0) {
            n_sent_count++;
            LOG_TRACE("Sent " + message.GetTypeString() + " to peer " + peer_info.str_peer_address);
        } else {
            LOG_ERROR("Failed to send " + message.GetTypeString() + " to peer " + peer_info.str_peer_address +
                      " (error: " + std::string(strerror(errno)) + ")");
        }
    }

    LOG_INFO("Broadcast sent to " + std::to_string(n_sent_count) + " of " +
             std::to_string(peer_sockets.size()) + " peers");

    return n_sent_count;
}
