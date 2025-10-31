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
    : n_socket(-1), peer_node(), f_connected(false), f_active(false), n_last_ping_nonce(0) {}

/**
 * @brief Construct peer with address and port
 * @param str_addr Peer IP address or hostname
 * @param n_port_num Peer listening port
 *
 * Creates peer connection object but does not establish connection.
 * Call ConnectToPeer() to actually connect.
 */
CPeerConnection::CPeerConnection(const std::string& str_addr, int n_port_num)
    : n_socket(-1), peer_node(str_addr, n_port_num), f_connected(false), f_active(false), n_last_ping_nonce(0) {}

/**
 * @brief Construct peer with CPeerNode
 * @param node Peer node information
 *
 * Creates peer connection object from existing CPeerNode.
 * Does not establish connection - call ConnectToPeer() to actually connect.
 */
CPeerConnection::CPeerConnection(const CPeerNode& node)
    : n_socket(-1), peer_node(node), f_connected(false), f_active(false), n_last_ping_nonce(0) {}

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
      m_thread(std::move(other.m_thread)),
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
      m_last_ping_time(std::chrono::steady_clock::now()) {
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
    if (!f_running) {
        return;
    }

    LOG_INFO("Stopping peer manager");
    f_stop_requested = true;
    f_stop_monitor = true;
    f_running = false;

    // Stop Boost.Asio I/O infrastructure
    // Destroy work guard first to allow io_context to complete
    m_io_work.reset();
    // Stop io_context event processing
    m_io_context.stop();

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

    // Wait for thread pool to finish all pending work
    if (m_thread_pool) {
        m_thread_pool->join();
    }

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

            // Create peer connection object for inbound peer (no thread for async I/O)
            auto p_peer = std::make_unique<CPeerConnection>(std::string(str_ip), n_peer_port);
            p_peer->n_socket = n_client_socket;
            p_peer->f_active = true;
            p_peer->f_connected = true;

            // Set connection_time for inbound peer
            int64_t n_now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            p_peer->peer_node.SetConnectionTime(n_now);
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
                LOG_WARN("io_context.run() exited unexpectedly, restarting...");
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
        // Create stream_descriptor to wrap the socket
        // This transfers ownership of the socket FD to Boost.Asio
        auto p_descriptor = std::make_shared<boost::asio::posix::stream_descriptor>(m_io_context, n_socket_fd);

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
        // Create stream_descriptor to wrap the socket
        // This transfers ownership of the socket FD to Boost.Asio
        auto p_descriptor = std::make_shared<boost::asio::posix::stream_descriptor>(m_io_context, n_socket_fd);

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
    std::shared_ptr<boost::asio::posix::stream_descriptor> p_descriptor;
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
                    LOG_INFO("Marked inbound peer " + p_peer->peer_node.GetAddress() + " as disconnected");
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
                    LOG_INFO("Marked inbound peer " + p_peer->peer_node.GetAddress() + " as disconnected (re-register failed)");
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
    std::shared_ptr<boost::asio::posix::stream_descriptor> p_descriptor;
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
        std::shared_ptr<boost::asio::posix::stream_descriptor> p_descriptor;
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
                    LOG_INFO("Marked inbound peer " + p_peer->peer_node.GetAddress() + " as disconnected (write error)");
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
            LOG_TRACE("Received " + msg_type + " message from peer " + p_peer->peer_node.GetAddress());

            // Calculate message size and remove from buffer
            // Message size = 1 (type_length) + type_length + 4 (payload_length) + payload_length
            size_t type_len = static_cast<uint8_t>(str_data[0]);
            size_t msg_size = 1 + type_len + 4 + received_msg.GetPayloadSize();
            str_data.erase(0, msg_size);

            // Handle different message types
            if (msg_type == MessageType::PING) {
                // Extract nonce from PING payload
                const std::vector<uint8_t>& payload = received_msg.GetPayloadBytes();
                uint32_t n_nonce = 0;
                if (payload.size() >= 4) {
                    n_nonce = (static_cast<uint32_t>(payload[0]) << 24) |
                             (static_cast<uint32_t>(payload[1]) << 16) |
                             (static_cast<uint32_t>(payload[2]) << 8) |
                             static_cast<uint32_t>(payload[3]);
                    LOG_INFO("Received " + msg_type + " with nonce " + std::to_string(n_nonce) +
                            " from peer " + p_peer->peer_node.GetAddress() + ", sending PONG");
                } else {
                    LOG_WARN("Unexpected " + msg_type + " from peer " + p_peer->peer_node.GetAddress() + ", ignore");
                    break;
                }

                // Respond with PONG containing the same nonce
                CPeerMessage pong_msg(MessageType::PONG, payload);

                // Send PONG via async I/O
                SendMessageAsync(n_socket_fd, pong_msg);

                LOG_TRACE("Queued PONG response with nonce " + std::to_string(n_nonce) +
                         " to peer " + p_peer->peer_node.GetAddress());

            } else if (msg_type == MessageType::PONG) {
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
                        p_peer->peer_node.SetPingRoundtripTime(d_roundtrip_ms);

                        LOG_INFO("Received PONG from peer " + p_peer->peer_node.GetAddress() +
                                " with matching nonce " + std::to_string(n_nonce) +
                                ", round-trip time: " + std::to_string(d_roundtrip_ms) + " ms");
                    } else {
                        LOG_WARN("Received PONG from peer " + p_peer->peer_node.GetAddress() +
                                " with nonce " + std::to_string(n_nonce) +
                                " but expected " + std::to_string(p_peer->n_last_ping_nonce));
                    }
                } else {
                    LOG_TRACE("Received PONG from peer " + p_peer->peer_node.GetAddress() + " (no nonce)");
                }
            } else if (msg_type == MessageType::TX_IDS) {
                std::string str_tx_ids = received_msg.GetPayloadString();
                LOG_INFO("Received transaction IDs from peer " + p_peer->peer_node.GetAddress() + ": " + str_tx_ids);
                // TODO: Process transaction IDs (e.g., request full transactions if not in mempool)
            } else {
                LOG_TRACE("Received unknown message type '" + msg_type + "' from peer " + p_peer->peer_node.GetAddress());
            }
        } else {
            // Not enough data yet for a complete message, wait for more
            // Note: In production, we should buffer this partial data per-socket
            LOG_TRACE("Incomplete message in buffer, waiting for more data (socket " + std::to_string(n_socket_fd) + ")");
            break;
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
        close(n_socket);
        return false;
    }

    if (connect(n_socket, (sockaddr*)&peer_addr, sizeof(peer_addr)) < 0) {
        LOG_ERROR("Failed to connect to peer " + str_address + ":" + std::to_string(n_port) + " - " + strerror(errno));
        close(n_socket);
        return false;
    }

    // Set socket to non-blocking mode (required for Boost.Asio async I/O)
    if (!SetSocketNonBlocking(n_socket, true)) {
        LOG_ERROR("Failed to set non-blocking mode for peer " + str_address);
        close(n_socket);
        return false;
    }

    // Create peer connection object
    auto p_peer = std::make_unique<CPeerConnection>(str_address, n_port);
    p_peer->n_socket = n_socket;
    p_peer->f_connected = true;
    p_peer->f_active = true;

    // Set connection_time to current timestamp
    int64_t n_now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    p_peer->peer_node.SetConnectionTime(n_now);
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

    LOG_INFO("Disconnected peer " + p_peer->peer_node.GetAddress());
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
            if (p_peer && p_peer->peer_node.GetAddress() == str_address && p_peer->peer_node.GetPort() == n_port) {
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
                             p_peer->peer_node.GetIdentifier());
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
                         p_peer->peer_node.GetIdentifier());
            }
        }
    }

    LOG_INFO("Sent " + MessageType::PING + " to " + std::to_string(n_sent) + " peers");
    return n_sent;
}
