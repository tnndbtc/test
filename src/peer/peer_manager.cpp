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
#include "peer/peer_message_factory.h"
#include "peer/messages/ping_message.h"
#include "peer/messages/pong_message.h"
#include "peer/messages/inventory_message.h"
#include "peer/messages/getdata_message.h"
#include "peer/messages/version_message.h"
#include "peer/messages/verack_message.h"
#include "peer/messages/tx_message.h"
#include "peer/messages/block_message.h"
#include "blockcore/protocol.h"
#include "utils/threadname.h"
#include "utils/hash.h"
#include "utils/network.h"
#include "utils/config.h"
#include "utils/get_public_ip.h"
#include "utils/time_util.h"
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

// ============= CPeerManager Implementation =============

/**
 * @brief Construct peer manager with listening port and max peers
 * @param n_port Port to listen on for inbound connections
 * @param n_max_outbound Maximum number of outbound peer connections
 * @param n_max_inbound Maximum number of inbound peer connections
 * @param n_ping_time Interval in seconds between PING messages
 * @param str_bind Bind IP address for P2P listening (empty means all interfaces)
 *
 * Initializes peer manager in stopped state. Reserves space for
 * peer connections to avoid vector reallocations during operation.
 * Call Start() to begin accepting connections.
 */
CPeerManager::CPeerManager(int n_port, int n_max_outbound, int n_max_inbound, int n_max_workers, int n_ping_time, uint32_t n_magic, const std::string& str_bind)
    : n_listen_port(n_port), str_bind_ip(str_bind), m_p_acceptor(nullptr),
      n_max_inbound_peers(n_max_inbound), n_max_outbound_peers(n_max_outbound),
      n_peers_ping_time(n_ping_time), m_n_network_magic(n_magic),
      f_running(false), f_stop_requested(false), f_stop_monitor(false),
      m_last_ping_time(TimeUtil::GetCurrentTime()),
      m_last_rotation_time(TimeUtil::GetCurrentTime()),
      p_blockweave(nullptr),
      m_str_public_ip("127.0.0.1"),
      m_last_public_ip_check(TimeUtil::GetCurrentTime()),
      m_n_services(NODE_NETWORK) {
    m_inbound_peers.reserve(n_max_inbound_peers);
    m_outbound_peers.reserve(n_max_outbound_peers);
    LOG_TRACE("m_last_rotation_time: " + std::to_string(m_last_rotation_time));

    // Initialize Boost.Asio I/O infrastructure for inbound connections
    m_io_work = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
        boost::asio::make_work_guard(m_io_context));
    m_thread_pool = std::make_unique<boost::asio::thread_pool>(n_max_workers);

    LOG_INFO("Initialized Boost.Asio with " + std::to_string(n_max_workers) + " worker threads for " + std::to_string(n_max_inbound_peers) + " max inbound connections");

    // Initialize public IP discovery
    CConfig config;
    std::string str_config_public_ip = config.GetPublicIP();
    m_vec_stun_addresses = config.GetStunAddresses();

    if (str_config_public_ip != "127.0.0.1") {
        // Manual override from config
        m_str_public_ip = str_config_public_ip;
        LOG_INFO("Using manually configured public IP: " + m_str_public_ip);
    } else if (m_n_network_magic != MAINNET_MAGIC && m_n_network_magic != TESTNET_MAGIC) {
        // LOCALNET or for unit test - no STUN needed
        m_str_public_ip = "127.0.0.1";
        LOG_INFO("LOCALNET mode: Using 127.0.0.1 as public IP");
    } else {
        // Auto-detect via STUN
        m_str_public_ip = ::GetPublicIP(m_vec_stun_addresses, 3000);  // Call global function with explicit timeout
        LOG_INFO("Discovered public IP via STUN: " + m_str_public_ip);
    }
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

    // Create acceptor for inbound connections
    if (!CreateAcceptor()) {
        LOG_ERROR("Failed to create acceptor for peer manager");
        return false;
    }

    f_running = true;
    f_stop_requested = false;

    // Start peer management thread
    m_peer_thread = std::thread(&CPeerManager::PeerManagerThread, this);

    // Start monitor thread for inbound socket I/O multiplexing and async accept
    m_monitor_inbound_thread = std::thread(&CPeerManager::MonitorInboundSocketThread, this);

    // Start async accept chain (runs in io_context, processed by monitor thread)
    StartAccept();

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

    // Close and destroy acceptor FIRST, before stopping io_context
    // This prevents the acceptor destructor from accessing io_context internals during shutdown
    CloseAcceptor();
    m_p_acceptor.reset();  // Explicitly destroy acceptor before io_context cleanup

    // Always clean up Boost.Asio resources, even if never started
    // This prevents hangs when CPeerManager is constructed but not started
    if (m_io_work) {
        m_io_work.reset();
    }
    m_io_context.stop();

    if (m_thread_pool) {
        m_thread_pool->join();
    }

    // Stop all peer connections (both inbound and outbound)
    // Close sockets and mark peers as disconnected
    {
        std::lock_guard<std::mutex> lock(cs_peers);

        // Stop inbound peers
        for (auto& p_peer : m_inbound_peers) {
            if (p_peer) {
                auto p_socket = p_peer->GetSocket();
                if (p_socket && p_socket->is_open()) {
                    // Cancel pending async operations (will trigger operation_aborted)
                    boost::system::error_code cancel_ec;
                    p_socket->cancel(cancel_ec);

                    // Close the socket
                    boost::system::error_code close_ec;
                    p_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, close_ec);
                    p_socket->close(close_ec);

                    LOG_TRACE("Cancelled and closed socket for peer: " + p_peer->GetIdentifier());
                }
                // No SetConnected(false) needed - peer lists cleared below
            }
        }

        // Stop outbound peers
        for (auto& p_peer : m_outbound_peers) {
            if (p_peer) {
                auto p_socket = p_peer->GetSocket();
                if (p_socket && p_socket->is_open()) {
                    // Cancel pending async operations
                    boost::system::error_code cancel_ec;
                    p_socket->cancel(cancel_ec);

                    // Close the socket
                    boost::system::error_code close_ec;
                    p_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, close_ec);
                    p_socket->close(close_ec);

                    LOG_TRACE("Cancelled and closed socket for peer: " + p_peer->GetIdentifier());
                }
                // No SetConnected(false) needed - peer lists cleared below
            }
        }
    }

    // Join threads (listener thread removed - accept handled by monitor thread)
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
 * @brief Create and configure Boost.Asio acceptor
 * @return true if acceptor created and bound successfully, false on error
 *
 * Acceptor creation sequence:
 * 1. Create Boost.Asio TCP acceptor bound to io_context
 * 2. Open acceptor for IPv4 TCP
 * 3. Set SO_REUSEADDR to allow rapid restart after shutdown
 * 4. Set SO_REUSEPORT (on non-Windows) for load balancing
 * 5. Bind to INADDR_ANY (all network interfaces) on configured port
 * 6. Start listening with backlog of 10 pending connections
 *
 * On any error, cleans up acceptor and returns false.
 */
bool CPeerManager::CreateAcceptor() {
    try {
        // Create acceptor
        m_p_acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(m_io_context);

        // Open for IPv4 TCP
        m_p_acceptor->open(boost::asio::ip::tcp::v4());

        // Set socket options
        m_p_acceptor->set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));

#ifndef _WIN32
        // Set SO_REUSEPORT on POSIX systems for better load balancing
        typedef boost::asio::detail::socket_option::boolean<SOL_SOCKET, SO_REUSEPORT> reuse_port;
        m_p_acceptor->set_option(reuse_port(true));
#endif

        // Bind to configured IP (or all interfaces if bind_ip is empty)
        boost::asio::ip::tcp::endpoint endpoint;
        if (str_bind_ip.empty()) {
            // Bind to all interfaces (0.0.0.0)
            endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::any(), n_listen_port);
        } else {
            // Bind to specific IP address
            boost::system::error_code ec;
            boost::asio::ip::address bind_address = boost::asio::ip::make_address(str_bind_ip, ec);
            if (ec) {
                throw boost::system::system_error(ec, "Invalid bind IP address: " + str_bind_ip);
            }
            endpoint = boost::asio::ip::tcp::endpoint(bind_address, n_listen_port);
        }
        m_p_acceptor->bind(endpoint);

        // Start listening with backlog of 10
        m_p_acceptor->listen(10);

        // Log the actual bind address
        auto local_ep = m_p_acceptor->local_endpoint();
        std::string str_bind_desc = local_ep.address().to_string() + ":" + std::to_string(local_ep.port());
        LOG_INFO("Acceptor created and listening on " + str_bind_desc);
        return true;

    } catch (const boost::system::system_error& e) {
        std::cerr << "[Peer Manager] Failed to create acceptor: " << e.what() << "\n";
        LOG_ERROR("Failed to create acceptor: " + std::string(e.what()));
        m_p_acceptor.reset();
        return false;
    }
}

/**
 * @brief Close acceptor and stop accepting connections
 *
 * Cancels any pending async_accept operations and closes the acceptor gracefully.
 * This causes the async accept chain to stop.
 */
void CPeerManager::CloseAcceptor() {
    if (m_p_acceptor && m_p_acceptor->is_open()) {
        boost::system::error_code ec;
        m_p_acceptor->cancel(ec);  // Cancel pending accepts
        m_p_acceptor->close(ec);   // Close acceptor
        LOG_INFO("Acceptor closed");
    }
}

/**
 * @brief Start async accept chain for incoming connections
 *
 * Initiates an async_accept operation on the acceptor. When a connection
 * is accepted, HandleAccept callback is invoked which processes the connection
 * and chains the next async_accept.
 */
void CPeerManager::StartAccept() {
    if (!m_p_acceptor || !m_p_acceptor->is_open()) {
        LOG_ERROR("Cannot start accept: acceptor is not open");
        return;
    }

    // Create a new socket for the incoming connection
    // The socket will be moved into the callback
    m_p_acceptor->async_accept(
        [this](const boost::system::error_code& ec, boost::asio::ip::tcp::socket socket) {
            HandleAccept(ec, std::move(socket));
        }
    );

    LOG_INFO("Async accept chain started");
}

/**
 * @brief Async accept completion handler
 * @param ec Error code from async_accept
 * @param socket Accepted socket (moved into callback)
 *
 * Callback invoked when async_accept completes. Performs:
 * 1. Error checking
 * 2. Ban checking
 * 3. Peer limit enforcement with subnet diversity logic
 * 4. Connection setup and registration
 * 5. Chains next async_accept
 *
 * Runs in io_context thread (MonitorInboundSocketThread).
 */
void CPeerManager::HandleAccept(const boost::system::error_code& ec, boost::asio::ip::tcp::socket socket) {
    // Check for errors
    if (ec) {
        if (ec == boost::asio::error::operation_aborted) {
            // Acceptor was closed - this is expected during shutdown
            LOG_INFO("Accept operation cancelled");
            return;
        }
        LOG_ERROR("Accept error: " + ec.message());

        // Chain next accept despite error (unless stopping)
        if (!f_stop_requested) {
            StartAccept();
        }
        return;
    }

    // Get peer address and port
    boost::system::error_code remote_ec;
    auto remote_endpoint = socket.remote_endpoint(remote_ec);
    if (remote_ec) {
        LOG_ERROR("Failed to get remote endpoint: " + remote_ec.message());
        socket.close();

        // Chain next accept
        if (!f_stop_requested) {
            StartAccept();
        }
        return;
    }

    std::string str_ip = remote_endpoint.address().to_string();
    int n_peer_port = remote_endpoint.port();

    // Check if peer is banned
    if (IsPeerBanned(str_ip)) {
        LOG_WARN("Rejecting connection from banned peer: " + str_ip);
        socket.close();

        // Chain next accept
        if (!f_stop_requested) {
            StartAccept();
        }
        return;
    }

    // Check if this peer already exists in our peer lists (prevent duplicates)
    {
        std::lock_guard<std::mutex> lock(cs_peers);

        // Check inbound peers
        for (const auto& p_peer : m_inbound_peers) {
            if (p_peer && p_peer->GetAddress() == str_ip && p_peer->GetPort() == n_peer_port) {
                LOG_WARN("Rejecting duplicate inbound connection from " + str_ip + ":" + std::to_string(n_peer_port) +
                         " (already in inbound peers list)");
                socket.close();
                if (!f_stop_requested) {
                    StartAccept();
                }
                return;
            }
        }

        // Check outbound peers
        for (const auto& p_peer : m_outbound_peers) {
            if (p_peer && p_peer->GetAddress() == str_ip && p_peer->GetPort() == n_peer_port) {
                LOG_WARN("Rejecting duplicate inbound connection from " + str_ip + ":" + std::to_string(n_peer_port) + 
                         " (already in outbound peers list)");
                socket.close();
                if (!f_stop_requested) {
                    StartAccept();
                }
                return;
            }
        }
    }

    // Create shared_ptr from moved socket (keep socket in Boost.Asio, no release)
    auto p_socket = std::make_shared<boost::asio::ip::tcp::socket>(std::move(socket));

    // Check peer limits and enforce subnet diversity
    // Collect peer to evict (if needed) while holding lock, then disconnect outside lock
    std::shared_ptr<CPeerNode> p_peer_to_evict;
    {
        std::lock_guard<std::mutex> lock(cs_peers);
        if (m_inbound_peers.size() >= static_cast<size_t>(n_max_inbound_peers)) {
            // Don't reject - instead, drop a random peer from same subnet (if any)
            std::string str_new_subnet = GetSubnet(str_ip);

            // Find peers from the same subnet
            std::vector<size_t> same_subnet_indices;
            for (size_t i = 0; i < m_inbound_peers.size(); i++) {
                if (m_inbound_peers[i] && m_inbound_peers[i]->IsConnected()) {
                    std::string str_peer_subnet = GetSubnet(m_inbound_peers[i]->GetAddress());
                    if (str_peer_subnet == str_new_subnet) {
                        same_subnet_indices.push_back(i);
                    }
                }
            }

            if (!same_subnet_indices.empty()) {
                // Drop a random peer from the same subnet
                std::srand(std::time(nullptr));
                size_t random_idx = same_subnet_indices[std::rand() % same_subnet_indices.size()];

                LOG_INFO("Max inbound peers reached. Dropping peer " + m_inbound_peers[random_idx]->GetIdentifier() + " from same subnet " + str_new_subnet +
                         " to accept new connection");

                p_peer_to_evict = m_inbound_peers[random_idx];
            } else {
                // No peer from same subnet - drop a random peer anyway for network diversity
                std::vector<size_t> connected_indices;
                for (size_t i = 0; i < m_inbound_peers.size(); i++) {
                    if (m_inbound_peers[i] && m_inbound_peers[i]->IsConnected()) {
                        connected_indices.push_back(i);
                    }
                }

                if (!connected_indices.empty()) {
                    std::srand(std::time(nullptr));
                    size_t random_idx = connected_indices[std::rand() % connected_indices.size()];

                    LOG_INFO("Max inbound peers reached. All peers from different subnets. "
                             "Dropping random peer for network diversity: " +
                             m_inbound_peers[random_idx]->GetIdentifier() +
                             " to accept new connection from " + str_ip);

                    p_peer_to_evict = m_inbound_peers[random_idx];
                } else {
                    // No connected peers - should not happen, but accept as fallback
                    LOG_ERROR("Maximum inbound peers reached but no connected peers found, add peer connection from " +
                             str_ip + ":" + std::to_string(n_peer_port));
                }
            }
        }
    }

    // Disconnect peer outside the lock to avoid deadlock
    // DisconnectPeer() needs cs_peers, so we must not hold it here
    if (p_peer_to_evict) {
        DisconnectPeer(p_peer_to_evict);
    }

    // Now register the new peer
    {
        std::lock_guard<std::mutex> lock(cs_peers);

        LOG_INFO("Accepted inbound peer connection from " + str_ip + ":" + std::to_string(n_peer_port));

        // Set socket keepalive on shared_ptr socket
        SetSocketKeepAlive(p_socket);

        // Create peer node with socket
        auto p_peer = std::make_shared<CPeerNode>(str_ip, n_peer_port, p_socket);
        // SetConnected to true after VERSION/VERACK handshakes
        p_peer->SetConnected(false);

        // Set connection_time for inbound peer
        int64_t n_now = TimeUtil::GetCurrentTime();
        p_peer->SetConnectionTime(n_now);
        p_peer->SetInbound(true);  // Mark as inbound connection
        LOG_INFO("Set connection_time for inbound peer " + str_ip + " to " + std::to_string(n_now));

        // Add to inbound peers list BEFORE registering socket
        // m_inbound_peers.push_back(p_peer);

        // Register socket with async I/O context (inside lock to ensure atomicity)
        RegisterSocket(p_peer);
        LOG_INFO("Registered inbound socket for peer: " + p_peer->GetIdentifier());
    }

    // Chain next async_accept (critical for continuous accepting)
    if (!f_stop_requested) {
        StartAccept();
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
bool CPeerManager::SetSocketKeepAlive(std::shared_ptr<boost::asio::ip::tcp::socket> p_socket) {
    if (!p_socket || !p_socket->is_open()) {
        LOG_ERROR("Cannot set keepalive on invalid or closed socket");
        return false;
    }

    // Extract native handle for platform-specific socket options
    int n_socket = p_socket->native_handle();

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
        LOG_TRACE("Peer management thread awakes");
        LOG_TRACE("PeerManagerThread begin loop: inbound_peers: " + std::to_string(m_inbound_peers.size()) + " outbound_peers: " + std::to_string(m_outbound_peers.size()));

        // Log detailed peer information
        {
            std::lock_guard<std::mutex> lock(cs_peers);
            std::string str_inbound_details = "Inbound peers: ";
            for (const auto& p_peer : m_inbound_peers) {
                if (p_peer) {
                    str_inbound_details += p_peer->GetIdentifier() + " ";
                }
            }
            LOG_TRACE(str_inbound_details);

            std::string str_outbound_details = "Outbound peers: ";
            for (const auto& p_peer : m_outbound_peers) {
                if (p_peer) {
                    str_outbound_details += p_peer->GetIdentifier() + " ";
                }
            }
            LOG_TRACE(str_outbound_details);
        }

        // Check if it's time to send PING messages (every n_peers_ping_time seconds)
        int64_t now = TimeUtil::GetCurrentTime();
        int64_t elapsed_ping = now - m_last_ping_time;

        if (elapsed_ping >= n_peers_ping_time) {
            // Send PING to all connected peers (periodic keep-alive)
            SendPingToAllPeers();
            m_last_ping_time = now;
        }

        // Check if it's time to update public IP (every 5 minutes, only for auto-detected IPs)
        // Skip if: LOCALNET mode, or public_ip was set in config
        CConfig config;
        bool f_is_manual_ip = (config.GetPublicIP() != "127.0.0.1");
        if (!f_is_manual_ip && m_n_network_magic != LOCALNET_MAGIC) {
            int64_t elapsed_ip = now - m_last_public_ip_check;
            if (elapsed_ip >= 300) {  // 5 minutes = 300 seconds
                std::string str_new_ip = ::GetPublicIP(m_vec_stun_addresses, 3000);
                if (str_new_ip != "127.0.0.1" && str_new_ip != m_str_public_ip) {
                    LOG_INFO("Public IP changed: " + m_str_public_ip + " -> " + str_new_ip);
                    m_str_public_ip = str_new_ip;
                } else if (str_new_ip != "127.0.0.1") {
                    LOG_TRACE("Public IP check: " + m_str_public_ip + " (unchanged)");
                }
                m_last_public_ip_check = now;
            }
        }

        // Periodic maintenance tasks

        // Rotate outbound connections (every 30 minutes)
        RotateOutboundConnections();

        // Clean up expired bans
        CleanupExpiredBans();

        // Sleep for a bit before next iteration
        LOG_TRACE("PeerManagerThread end loop inbound_peers: " + std::to_string(m_inbound_peers.size()) + " outbound_peers: " + std::to_string(m_outbound_peers.size()));
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    LOG_TRACE("Peer management thread stopped");
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
                LOG_TRACE("io_context.run() exited unexpectedly, restarting...");
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
void CPeerManager::RegisterSocket(std::shared_ptr<CPeerNode> p_peer) {
    try {
        auto p_socket = p_peer->GetSocket();
        if (!p_socket || !p_socket->is_open()) {
            LOG_ERROR("Cannot register invalid or closed socket");
            return;
        }

        LOG_INFO("Registered socket for async I/O: " + p_peer->GetIdentifier());

        // Allocate read buffer (8KB for P2P messages)
        auto p_buffer = std::make_shared<std::vector<uint8_t>>(8192);

        // Start async read operation
        // When data arrives, HandleAsyncRead will be called
        // Capture shared_ptr<CPeerNode> to keep peer alive during async operation
        p_socket->async_read_some(
            boost::asio::buffer(*p_buffer),
            [this, p_peer, p_buffer](const boost::system::error_code& ec, size_t n_bytes_transferred) {
                HandleAsyncRead(ec, n_bytes_transferred, p_peer, p_buffer);
            }
        );

        LOG_TRACE("Started async_read_some for peer: " + p_peer->GetIdentifier());
    }
    catch (const std::exception& e) {
        LOG_ERROR("Failed to register inbound socket for peer " + p_peer->GetIdentifier() + ": " + std::string(e.what()));

        // Close socket if registration failed
        auto p_socket = p_peer->GetSocket();
        if (p_socket && p_socket->is_open()) {
            boost::system::error_code ec;
            p_socket->close(ec);
        }
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
                                    std::shared_ptr<CPeerNode> p_peer,
                                    std::shared_ptr<std::vector<uint8_t>> p_buffer) {

    auto p_socket = p_peer->GetSocket();

    // Check if socket is still valid
    if (!p_socket || !p_socket->is_open()) {
        LOG_TRACE("HandleAsyncRead: Socket for peer " + p_peer->GetIdentifier() + " already closed");
        return;
    }

    // Handle error cases
    if (ec) {
        if (ec == boost::asio::error::eof) {
            LOG_INFO("Peer disconnected (EOF): " + p_peer->GetIdentifier());
        }
        else if (ec == boost::asio::error::operation_aborted) {
            LOG_TRACE("Async read aborted for peer: " + p_peer->GetIdentifier() + " (shutdown)");
        }
        else {
            LOG_ERROR("Async read error for peer " + p_peer->GetIdentifier() + ": " + ec.message());
        }

        // Disconnect peer and remove from lists
        DisconnectPeer(p_peer);
        return;
    }

    // Success: Data received
    if (n_bytes_transferred > 0) {
        LOG_TRACE("Received " + std::to_string(n_bytes_transferred) +
                  " bytes from peer: " + p_peer->GetIdentifier());

        // Post work to thread pool to process the received data
        boost::asio::post(*m_thread_pool, [this, p_peer, p_buffer, n_bytes_transferred]() {
            ProcessReceivedMessage(p_peer, p_buffer, n_bytes_transferred);
        });
    }

    // Re-register for next async read (keep connection alive)
    try {
        // Allocate new buffer for next read
        auto p_new_buffer = std::make_shared<std::vector<uint8_t>>(8192);

        // Capture peer to keep it alive during async operation
        p_socket->async_read_some(
            boost::asio::buffer(*p_new_buffer),
            [this, p_peer, p_new_buffer](const boost::system::error_code& ec, size_t n_bytes) {
                HandleAsyncRead(ec, n_bytes, p_peer, p_new_buffer);
            }
        );

        LOG_TRACE("Re-registered async_read_some for peer: " + p_peer->GetIdentifier());
    }
    catch (const std::exception& e) {
        LOG_ERROR("Failed to re-register async read for peer " + p_peer->GetIdentifier() +
                  ": " + std::string(e.what()));

        // Disconnect peer and remove from lists
        DisconnectPeer(p_peer);
    }
}

/**
 * @brief Send message asynchronously to inbound peer
 * @param p_peer Peer node to send message to
 * @param message CPeerMessage to send
 *
 * Serializes the message and sends it asynchronously using Boost.Asio.
 * This is used ONLY for inbound peers (outbound peers use sync send).
 * Thread-safe operation with mutex protection.
 */
void CPeerManager::SendMessageAsync(std::shared_ptr<CPeerNode> p_peer,
                                    const CPeerMessage& message) {
    auto p_socket = p_peer->GetSocket();
    if (!p_socket || !p_socket->is_open()) {
        LOG_WARN("SendMessageAsync: Socket for peer " + p_peer->GetIdentifier() + " is closed");
        return;
    }

    // Serialize message
    std::string str_serialized = message.Serialize();

    if (str_serialized.empty()) {
        LOG_ERROR("Failed to serialize message for peer: " + p_peer->GetIdentifier());
        return;
    }

    // Create shared buffer to keep data alive during async operation
    // Convert string to vector<uint8_t>
    auto p_buffer = std::make_shared<std::vector<uint8_t>>(str_serialized.begin(), str_serialized.end());

    // Send asynchronously, capture peer to keep it alive
    boost::asio::async_write(
        *p_socket,
        boost::asio::buffer(*p_buffer),
        [this, p_peer, p_buffer](const boost::system::error_code& ec, size_t n_bytes_transferred) {
            HandleAsyncWrite(ec, n_bytes_transferred, p_peer);
        }
    );

    LOG_TRACE("Queued async write of " + std::to_string(p_buffer->size()) +
              " bytes to peer: " + p_peer->GetIdentifier());
}

/**
 * @brief Async write completion handler for inbound sockets
 * @param ec Boost error code from async operation
 * @param n_bytes_transferred Number of bytes written
 * @param p_peer Peer node for this connection
 *
 * Called when async_write completes (successfully or with error).
 * Logs result and handles errors by closing the connection.
 */
void CPeerManager::HandleAsyncWrite(const boost::system::error_code& ec,
                                     size_t n_bytes_transferred,
                                     std::shared_ptr<CPeerNode> p_peer) {
    if (ec) {
        if (ec == boost::asio::error::operation_aborted) {
            LOG_TRACE("Async write aborted on peer: " + p_peer->GetIdentifier() + " (shutdown)");
        }
        else {
            LOG_ERROR("Async write error on peer: " + p_peer->GetIdentifier() + ": " + ec.message());
        }

        // Disconnect peer and remove from lists
        DisconnectPeer(p_peer);
        return;
    }

    // Success
    LOG_TRACE("Async write completed: " + std::to_string(n_bytes_transferred) +
              " bytes sent to peer: " + p_peer->GetIdentifier());
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
void CPeerManager::ProcessReceivedMessage(std::shared_ptr<CPeerNode> p_peer,
                                          std::shared_ptr<std::vector<uint8_t>> p_buffer,
                                          size_t n_bytes_received) {
    // Set thread name for worker thread using integer ID (like p2p_worker0, p2p_worker1, etc.)
    static std::atomic<int> s_worker_id_counter{0};
    static thread_local int s_worker_id = s_worker_id_counter.fetch_add(1);
    static thread_local bool thread_name_set = false;
    if (!thread_name_set) {
        // at process level, the thread name can only be changed when boost
        // calls this function, because this thread is maintained by boost
        SetThreadName("p2p_worker" + std::to_string(s_worker_id));
        thread_name_set = true;
    }

    LOG_TRACE("Processing " + std::to_string(n_bytes_received) + " bytes from peer: " + p_peer->GetIdentifier());

    // Convert buffer to string for message parsing
    // Note: In production, we should handle partial messages and buffering per-socket
    std::string str_data(p_buffer->begin(), p_buffer->begin() + n_bytes_received);

    // Try to deserialize complete messages from buffer
    // CPeerMessage format: [4 bytes magic][1 byte type_length][N bytes type][4 bytes payload_length][M bytes payload]
    while (str_data.size() >= CPeerMessage::GetMinHeaderSize()) {
        // Use factory to create appropriate message type
        std::unique_ptr<CPeerMessage> p_received_msg = CPeerMessageFactory::CreateFromWireData(str_data, m_n_network_magic);

        if (p_received_msg) {
            // Successfully deserialized a message
            std::string msg_type = p_received_msg->GetType();
            LOG_TRACE("Received " + msg_type + " message from peer " + p_peer->GetIdentifier());

            // Calculate message size and remove from buffer
            // Message size = 4 (magic) + 1 (type_length) + type_length + 4 (payload_length) + payload_length
            size_t type_len = static_cast<uint8_t>(str_data[4]);  // Skip 4 bytes of magic
            std::vector<uint8_t> payload = p_received_msg->SerializePayload();
            size_t msg_size = 4 + 1 + type_len + 4 + payload.size();
            str_data.erase(0, msg_size);

            if (msg_type != MessageType::VERSION &&
                msg_type != MessageType::VERACK) {
                if (!p_peer->IsHandshakeComplete()) {
                    // Other messages only allowed after VERSION/VERACK handshake completes
                    LOG_WARN("Received " + msg_type + " from peer " + p_peer->GetIdentifier() +
                             " before handshake complete - disconnecting");
                    DisconnectPeer(p_peer);
                    return;
                }
            }
            // Handle different message types
            if (msg_type == MessageType::PING) {
                HandlePingMessage(p_peer, *p_received_msg);
            } else if (msg_type == MessageType::PONG) {
                HandlePongMessage(p_peer, *p_received_msg);
            } else if (msg_type == MessageType::INVENTORY) {
                HandleInventoryMessage(p_peer, *p_received_msg);
            } else if (msg_type == MessageType::VERSION) {
                HandleVersionMessage(p_peer, *p_received_msg);
            } else if (msg_type == MessageType::VERACK) {
                HandleVerackMessage(p_peer, *p_received_msg);
            } else if (msg_type == MessageType::GETDATA) {
                HandleGetDataMessage(p_peer, *p_received_msg);
            } else if (msg_type == MessageType::TX) {
                HandleTxMessage(p_peer, *p_received_msg);
            } else if (msg_type == MessageType::BLOCK) {
                HandleBlockMessage(p_peer, *p_received_msg);
            } else {
                LOG_TRACE("Received unknown message type '" + msg_type + "' from peer " + p_peer->GetIdentifier());
            }
        } else {
            // Corrupted message
            LOG_WARN("Incorrect message in buffer, disconnect the peer: " + p_peer->GetIdentifier());
            DisconnectPeer(p_peer);
            break;
        }
    }
}

// ============================================================================
// Message Handler Helper Methods
// ============================================================================

void CPeerManager::HandlePingMessage(std::shared_ptr<CPeerNode> p_peer_shared,
                                     const CPeerMessage& received_msg) {
    // Cast to CPingMessage for type-safe access
    const auto& ping_msg = static_cast<const CPingMessage&>(received_msg);
    uint32_t n_nonce = ping_msg.GetNonce();

    LOG_TRACE("Received PING with nonce " + std::to_string(n_nonce) +
            " from peer " + p_peer_shared->GetIdentifier() + ", sending PONG");

    // Respond with PONG containing the same nonce
    CPongMessage pong_msg(n_nonce, m_n_network_magic);

    // Send PONG via async I/O
    SendMessageAsync(p_peer_shared, pong_msg);

    LOG_TRACE("Queued PONG response with nonce " + std::to_string(n_nonce) +
             " to peer " + p_peer_shared->GetIdentifier());
}

void CPeerManager::HandlePongMessage(std::shared_ptr<CPeerNode> p_peer_shared, const CPeerMessage& received_msg) {
    // Cast to CPongMessage for type-safe access
    const auto& pong_msg = static_cast<const CPongMessage&>(received_msg);
    uint32_t n_nonce = pong_msg.GetNonce();

    // Verify nonce matches last sent PING
    if (n_nonce == p_peer_shared->GetLastPingNonce()) {
        // Calculate round-trip time
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            now - p_peer_shared->GetLastPingSendTime());
        double d_roundtrip_ms = duration.count() / 1000.0;

        // Update peer node with ping round-trip time
        p_peer_shared->SetPingRoundtripTime(d_roundtrip_ms);

        LOG_TRACE("Received PONG from peer " + p_peer_shared->GetIdentifier() +
                " with matching nonce " + std::to_string(n_nonce) +
                ", round-trip time: " + std::to_string(d_roundtrip_ms) + " ms");
    } else {
        LOG_WARN("Received PONG from peer " + p_peer_shared->GetIdentifier() +
                " with nonce " + std::to_string(n_nonce) +
                " but expected " + std::to_string(p_peer_shared->GetLastPingNonce()));
    }
}

void CPeerManager::HandleInventoryMessage(std::shared_ptr<CPeerNode> p_peer_shared,
                                         const CPeerMessage& received_msg) {
    // Cast to CInventoryMessage for type-safe access
    const auto& inv_msg = static_cast<const CInventoryMessage&>(received_msg);
    const auto& items = inv_msg.GetItems();

    LOG_INFO("Received INVENTORY from peer " + p_peer_shared->GetIdentifier() + ": " +
             std::to_string(items.size()) + " items");

    // Track all inventory items for relay
    std::vector<std::pair<ObjectType::Type, std::string>> vec_inventory;

    // Track missing items for GETDATA request
    std::vector<std::pair<ObjectType::Type, std::string>> vec_missing_items;

    // Process each inventory item
    for (const auto& item : items) {
        ObjectType::Type obj_type = item.type;
        const std::string& str_hash = item.str_hash;

        // Add to inventory list for relay
        CHash hash_trace(reinterpret_cast<const unsigned char*>(str_hash.data()), str_hash.size());
        LOG_TRACE("HandleInventoryMessage: received notification for object type: " + std::to_string(obj_type) + " hash: " + hash_trace.GetData()+ " from peer: " + p_peer_shared->GetIdentifier());

        // Mark this peer as knowing about this inventory
        MarkInventoryKnown(p_peer_shared, obj_type, str_hash);

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
            // f_need_item = true;
            // There is no point asking blocks/transactions that cannot be stored by blockwave
            // TODO: question, should it still ask for broadcast purpose in future?
            return;
        }

        if (f_need_item) {
            vec_missing_items.push_back({obj_type, str_hash});
        }
    }

    LOG_INFO("Processed " + std::to_string(items.size()) + " inventory items from peer " +
             p_peer_shared->GetIdentifier() + " (" +
             std::to_string(vec_missing_items.size()) + " missing, " +
             std::to_string(items.size() - vec_missing_items.size()) + " already have)");

    // Request missing items via GETDATA (scheduled asynchronously)
    if (!vec_missing_items.empty()) {
        std::vector<std::pair<ObjectType::Type, std::string>> items = vec_missing_items;
        size_t item_count = items.size();
        std::string peer_addr = p_peer_shared->GetIdentifier();

        // Capture peer to keep it alive during async operation
        boost::asio::post(*m_thread_pool, [this, p_peer_shared, items, item_count, peer_addr]() {
            ScheduleGetDataMessage(p_peer_shared, items);
            LOG_INFO("Sent GETDATA request for " + std::to_string(item_count) +
                     " items to peer " + peer_addr);
        });
    } else {
        LOG_TRACE("No GETDATA needed - already have all inventory items from peer " +
                  p_peer_shared->GetIdentifier());
    }
}

void CPeerManager::HandleVersionMessage(std::shared_ptr<CPeerNode> p_peer_shared, const CPeerMessage& received_msg) {
    // Cast to CVersionMessage for type-safe access
    const auto& version_msg = static_cast<const CVersionMessage&>(received_msg);

    // Extract version information
    int32_t n_protocol_version = version_msg.GetProtocolVersion();
    uint64_t n_services = version_msg.GetAddressFrom().n_services;
    int32_t n_chain_tip_height = version_msg.GetChainTipHeight();

    LOG_INFO("Received VERSION from peer " + p_peer_shared->GetIdentifier() +
             ": protocol=" + std::to_string(n_protocol_version) +
             ", services=" + std::to_string(n_services) +
             ", chain_height=" + std::to_string(n_chain_tip_height));

    // Store protocol version and services in peer node
    p_peer_shared->SetProtocolVersion(n_protocol_version);
    p_peer_shared->SetServices(n_services);

    // Add VERSION_RCVD flag to handshake state bitmap
    if (p_peer_shared->AddHandshakeFlag(HandshakeState::VERSION_RCVD) == false) {
        DisconnectPeer(p_peer_shared);
        return;
    }

    // Check if peer is in outbound peers
    bool f_is_outbound = false;
    {
        std::lock_guard<std::mutex> lock(cs_peers);
        for (const auto& p : m_outbound_peers) {
            if (p == p_peer_shared) {
                f_is_outbound = true;
                break;
            }
        }
    }

    if (f_is_outbound) {
        // Node A receiving VERSION from Node B: send VERACK
        CVerackMessage verack(m_n_network_magic);
        SendMessageAsync(p_peer_shared, verack);
        LOG_INFO("Sent VERACK to outbound peer " + p_peer_shared->GetIdentifier());
    } else {
        // Not in outbound peers, check if in inbound peers
        bool f_in_inbound = false;
        {
            std::lock_guard<std::mutex> lock(cs_peers);
            for (const auto& p : m_inbound_peers) {
                if (p == p_peer_shared) {
                    f_in_inbound = true;
                    break;
                }
            }
        }

        if (!f_in_inbound) {
            // Not in inbound peers - add it
            std::lock_guard<std::mutex> lock(cs_peers);
            m_inbound_peers.push_back(p_peer_shared);
            LOG_INFO("Added peer " + p_peer_shared->GetIdentifier() + " to inbound peers list");
        }

        // Node B receiving VERSION from Node A: send VERSION response, then VERACK
        CVersionMessage response_version(m_n_network_magic);
        response_version.SetAddressRecv(version_msg.GetAddressFrom());
        CNetworkAddress our_addr(m_n_services, m_str_public_ip, n_listen_port);
        response_version.SetAddressFrom(our_addr);
        if (p_blockweave) {
            int32_t n_chain_height = static_cast<int32_t>(p_blockweave->GetBlockCount()) - 1;
            response_version.SetChainTipHeight(n_chain_height >= 0 ? n_chain_height : 0);
        }
        SendMessageAsync(p_peer_shared, response_version);
        if (p_peer_shared->AddHandshakeFlag(HandshakeState::VERSION_SENT) == false) { // Add VERSION_SENT flag
            DisconnectPeer(p_peer_shared);
            return;
        }
        LOG_INFO("Sent VERSION response to inbound peer " + p_peer_shared->GetIdentifier());

        // Then send VERACK
        CVerackMessage verack(m_n_network_magic);
        SendMessageAsync(p_peer_shared, verack);
        LOG_INFO("Sent VERACK to inbound peer " + p_peer_shared->GetIdentifier());
    }
}

void CPeerManager::HandleVerackMessage(std::shared_ptr<CPeerNode> p_peer_shared,
                                       const CPeerMessage& /* received_msg */) {
    LOG_INFO("Received VERACK from peer " + p_peer_shared->GetIdentifier());

    // Add VERACK_RCVD flag to handshake state bitmap
    if (p_peer_shared->AddHandshakeFlag(HandshakeState::VERACK_RCVD) == false) {
        DisconnectPeer(p_peer_shared);
        return;
    }
}

void CPeerManager::HandleGetDataMessage(std::shared_ptr<CPeerNode> p_peer_shared,
                                       const CPeerMessage& received_msg) {
    // Cast to CGetDataMessage for type-safe access
    const auto& getdata_msg = static_cast<const CGetDataMessage&>(received_msg);
    const auto& items = getdata_msg.GetItems();

    LOG_INFO("Received GETDATA from peer " + p_peer_shared->GetIdentifier() + ": " +
             std::to_string(items.size()) + " items");

    // Convert to requested items format
    std::vector<std::pair<ObjectType::Type, std::string>> vec_requested_items;

    for (const auto& item : items) {
        CHash hash_trace(reinterpret_cast<const unsigned char*>(item.str_hash.data()), item.str_hash.size());
        LOG_TRACE("HandleGetDataMessage: received request for object type: " + std::to_string(item.type) + " hash: " + hash_trace.GetData());

        vec_requested_items.push_back({item.type, item.str_hash});
    }

    LOG_INFO("Peer " + p_peer_shared->GetIdentifier() + " requested " +
             std::to_string(vec_requested_items.size()) + " items via GETDATA");

    // Schedule response asynchronously to avoid blocking
    std::string peer_addr = p_peer_shared->GetIdentifier();
    std::vector<std::pair<ObjectType::Type, std::string>> requested_items = vec_requested_items;

    // Capture peer to keep it alive during async operation
    boost::asio::post(*m_thread_pool, [this, p_peer_shared, peer_addr, requested_items]() {
        // Send each block and transaction individually in the loop
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
                        // Send individual TX message
                        CTxMessage tx_msg(p_tx, m_n_network_magic);
                        SendMessageAsync(p_peer_shared, tx_msg);
                        LOG_TRACE("Sent TX message with transaction " + hash.GetData() + "... to peer " + peer_addr);
                    } else {
                        LOG_TRACE("Transaction " + hash.GetData() + "... not found in mempool for peer " + peer_addr);
                    }
                }
            } else if (obj_type == ObjectType::BLOCK) {
                // Query block from blockweave
                if (p_blockweave) {
                    std::shared_ptr<CBlock> p_block = p_blockweave->GetBlock(hash);
                    if (p_block) {
                        // Send individual BLOCK message
                        CBlockMessage block_msg(p_block, m_n_network_magic);
                        SendMessageAsync(p_peer_shared, block_msg);
                        LOG_TRACE("Sent BLOCK message with block " + hash.GetData() + "... to peer " + peer_addr);
                    } else {
                        LOG_TRACE("Block " + hash.GetData() + "... not found for peer " + peer_addr);
                    }
                }
            }
        }
    });
}

void CPeerManager::HandleTxMessage(std::shared_ptr<CPeerNode> p_peer_shared, const CPeerMessage& received_msg) {
    // Cast to CTxMessage for type-safe access
    const auto& tx_msg = static_cast<const CTxMessage&>(received_msg);
    std::shared_ptr<CTransaction> p_tx = tx_msg.GetTransaction();

    if (!p_tx) {
        LOG_WARN("Failed to get transaction from TX message from peer " + p_peer_shared->GetIdentifier());
        return;
    }

    LOG_INFO("Received TX from peer " + p_peer_shared->GetIdentifier() +
             " (transaction ID: " + p_tx->m_id.GetData() + "...)");

    // Add transaction to blockweave mempool
    if (p_blockweave) {
        p_blockweave->AddTransaction(p_tx);
        LOG_TRACE("Added transaction " + p_tx->m_id.GetData() + "(from: " +  p_tx->m_str_owner + " to: " + p_tx->m_str_target + ") ... to mempool");

        // Broadcast INVENTORY for the new transaction (scheduled asynchronously)
        std::vector<std::pair<ObjectType::Type, std::string>> vec_inventory;
        vec_inventory.push_back({ObjectType::TRANSACTION,
            std::string(reinterpret_cast<const char*>(p_tx->m_id.GetBytes().data()), 32)});

        boost::asio::post(*m_thread_pool, [this, vec_inventory]() {
            LOG_TRACE("Broadcasted INVENTORY for received transaction");
            BroadcastInventoryByPeerKnowledge(vec_inventory);
        });
    }
}

void CPeerManager::HandleBlockMessage(std::shared_ptr<CPeerNode> p_peer_shared, const CPeerMessage& received_msg) {
    // Cast to CBlockMessage for type-safe access
    const auto& block_msg = static_cast<const CBlockMessage&>(received_msg);
    std::shared_ptr<CBlock> p_block = block_msg.GetBlock();

    if (!p_block) {
        LOG_WARN("Failed to get block from BLOCK message from peer " + p_peer_shared->GetIdentifier());
        return;
    }

    LOG_INFO("Received BLOCK from peer " + p_peer_shared->GetIdentifier() +
             " (block #" + std::to_string(p_block->GetHeight()) +
             ", hash: " + p_block->GetHash().GetData() + "... miner: " + p_block->GetMiner() + ")");

    LOG_TRACE("Block contains " + std::to_string(p_block->GetTransactions().size()) +
             " transactions");

    // Verify and add block to blockchain
    if (p_blockweave) {
        auto [f_success, vec_blocks_to_broadcast] = p_blockweave->VerifyBlock(p_block);

        if (f_success) {
            LOG_INFO("Block #" + std::to_string(p_block->GetHeight()) + " accepted into blockchain");

            // Broadcast INVENTORY for all accepted blocks (original + processed orphans)
            if (!vec_blocks_to_broadcast.empty()) {
                std::vector<std::pair<ObjectType::Type, std::string>> vec_inventory;
                for (const auto& p_broadcast_block : vec_blocks_to_broadcast) {
                    vec_inventory.push_back({ObjectType::BLOCK,
                        std::string(reinterpret_cast<const char*>(p_broadcast_block->GetHash().GetBytes().data()), 32)});
                }

                boost::asio::post(*m_thread_pool, [this, vec_inventory]() {
                    BroadcastInventoryByPeerKnowledge(vec_inventory);
                    LOG_TRACE("Broadcasted INVENTORY for " + std::to_string(vec_inventory.size()) + " accepted blocks");
                });
            }
        } else {
            LOG_INFO("Block #" + std::to_string(p_block->GetHeight()) +
                     " verification failed or added to orphan pool");
        }
    } else {
        LOG_WARN("Cannot verify block: blockweave not initialized");
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
    try {
        // Create Boost.Asio socket
        auto p_socket = std::make_shared<boost::asio::ip::tcp::socket>(m_io_context);

        // Create remote endpoint first to determine protocol
        boost::system::error_code addr_ec;
        auto addr = boost::asio::ip::make_address(str_address, addr_ec);
        if (addr_ec) {
            LOG_ERROR("Invalid peer address: " + str_address + " - " + addr_ec.message());
            return false;
        }

        boost::asio::ip::tcp::endpoint endpoint(addr, n_port);

        // --- Choose local binding IP, to honor bind_ip  ---
        if (str_bind_ip.empty() == false) {
            boost::system::error_code ec_local;
            boost::asio::ip::address local_addr =
                boost::asio::ip::make_address(str_bind_ip, ec_local);
            if (ec_local) {
                LOG_ERROR("Invalid local bind IP: " + str_bind_ip + " - " + ec_local.message());
                return false;
            }

            // Verify protocol compatibility between local and remote addresses
            if (local_addr.is_v4() != addr.is_v4()) {
                LOG_ERROR("Protocol mismatch: bind_ip is " +
                         std::string(local_addr.is_v4() ? "IPv4" : "IPv6") +
                         " but remote address " + str_address + " is " +
                         std::string(addr.is_v4() ? "IPv4" : "IPv6"));
                return false;
            }

            // Bind socket to chosen local IP (ephemeral port 0)
            // Use remote endpoint's protocol to ensure compatibility
            boost::asio::ip::tcp::endpoint local_endpoint(local_addr, 0);
            p_socket->open(endpoint.protocol());  // Use remote endpoint's protocol
            p_socket->bind(local_endpoint, ec_local);
            if (ec_local) {
                LOG_ERROR("Failed to bind local endpoint " + str_bind_ip + ": " + ec_local.message());
                return false;
            }
        }

        // Connect to peer (synchronous for now)
        boost::system::error_code ec;
        p_socket->connect(endpoint, ec);

        if (ec) {
            LOG_ERROR("Failed to connect to peer " + str_address + ":" + std::to_string(n_port) + " - " + ec.message());
            return false;
        }

        // Log connection details
        auto local_ep = p_socket->local_endpoint();
        auto remote_ep = p_socket->remote_endpoint();
        LOG_INFO("Socket connected: " + local_ep.address().to_string() + ":" + std::to_string(local_ep.port()) +
                  " -> " + remote_ep.address().to_string() + ":" + std::to_string(remote_ep.port()));

        // Set socket keepalive
        if (!SetSocketKeepAlive(p_socket)) {
            LOG_WARN("Failed to set keepalive for peer " + str_address);
        }

        // Create peer connection object with socket
        auto p_peer = std::make_shared<CPeerNode>(str_address, n_port, p_socket);

        // Set connection_time to current timestamp
        int64_t n_now = TimeUtil::GetCurrentTime();
        p_peer->SetConnectionTime(n_now);
        p_peer->SetInbound(false);  // Mark as outbound connection
        LOG_INFO("Set connection_time for outbound peer " + p_peer->GetIdentifier() + " to " + std::to_string(n_now));

        // Add to outbound peers list
        {
            std::lock_guard<std::mutex> lock(cs_peers);
            auto it = std::find(m_outbound_peers.begin(), m_outbound_peers.end(), nullptr);
            bool f_erased_nullptr = false;
            if (it != m_outbound_peers.end()) {
                m_outbound_peers.erase(it);
                f_erased_nullptr = true;
            }
            if (f_erased_nullptr == false)
            {
                LOG_ERROR("Expect peer reservation is made, but couldn't find for peer: " + p_peer->GetIdentifier());
            }
            LOG_INFO("Add peer: " +  p_peer->GetIdentifier() + " to outbound peers");
            m_outbound_peers.push_back(p_peer);
        }

        // Register socket with async I/O context (so we can receive VERSION response)
        RegisterSocket(p_peer);

        // Send VERSION message
        CVersionMessage version_msg(m_n_network_magic);
        CNetworkAddress addr_recv(0, str_address, n_port);  // services=0 for peer we're connecting to
        CNetworkAddress addr_from(m_n_services, m_str_public_ip, n_listen_port);
        version_msg.SetAddressRecv(addr_recv);
        version_msg.SetAddressFrom(addr_from);
        if (p_blockweave) {
            int32_t n_chain_height = static_cast<int32_t>(p_blockweave->GetBlockCount()) - 1;
            version_msg.SetChainTipHeight(n_chain_height >= 0 ? n_chain_height : 0);
        }
        SendMessageAsync(p_peer, version_msg);
        if (p_peer->AddHandshakeFlag(HandshakeState::VERSION_SENT) == false) { // Add VERSION_SENT flag
            DisconnectPeer(p_peer);
            return false;
        }
        LOG_INFO("Sent VERSION to " + p_peer->GetIdentifier() + " (waiting for VERSION/VERACK)");

        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Exception connecting to peer " + str_address + ":" + std::to_string(n_port) + " - " + std::string(e.what()));
        return false;
    }
}

/**
 * @brief Disconnect from peer and cleanup resources
 * @param p_peer Pointer to peer connection to disconnect
 *
 * Disconnection sequence:
 * 1. Mark as disconnected
 * 2. Shutdown socket (SHUT_RDWR)
 * 3. Close socket
 * 4. Remove from peer lists (m_inbound_peers and m_outbound_peers)
 *
 * Safe to call on NULL pointer (no-op).
 * Thread-safe: Uses cs_peers mutex for list manipulation.
 */
void CPeerManager::DisconnectPeer(std::shared_ptr<CPeerNode> p_peer_shared) {
    if (!p_peer_shared) {
        return;
    }

    p_peer_shared->SetConnected(false);

    // Close socket
    auto p_socket = p_peer_shared->GetSocket();
    if (p_socket && p_socket->is_open()) {
        boost::system::error_code ec;
        p_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        p_socket->close(ec);
    }

    // Remove peer from lists immediately (prevents race condition with background cleanup)
    {
        std::lock_guard<std::mutex> lock(cs_peers);

        // Remove from inbound peers
        auto pred = [p_peer_shared](const std::shared_ptr<CPeerNode>& p) {
            return p == p_peer_shared;
        };

        m_inbound_peers.erase(
            std::remove_if(m_inbound_peers.begin(), m_inbound_peers.end(), pred),
            m_inbound_peers.end()
        );

        // Remove from outbound peers
        m_outbound_peers.erase(
            std::remove_if(m_outbound_peers.begin(), m_outbound_peers.end(), pred),
            m_outbound_peers.end()
        );
    }

    LOG_INFO("Disconnected peer " + p_peer_shared->GetIdentifier());
    LOG_INFO("Outbound peer size after remove: " + std::to_string(m_outbound_peers.size()));
    LOG_INFO("Inbound peer size after remove: " + std::to_string(m_inbound_peers.size()));
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
    // Check if we've reached max outbound peers and drop a random one if needed
    // We need to do this atomically to prevent race conditions where
    // multiple threads could pass the size check simultaneously
    std::shared_ptr<CPeerNode> p_peer_to_drop;
    {
        std::lock_guard<std::mutex> lock(cs_peers);

        // Check if already connected to this peer
        for (const auto& p_peer : m_outbound_peers) {
            if (p_peer && p_peer->GetAddress() == str_address && p_peer->GetPort() == n_port) {
                LOG_INFO("Already connected to peer " + str_address + ":" + std::to_string(n_port));
                return false;
            }
        }

        // If max outbound peers reached, drop a random one to make room for new peer
        if (m_outbound_peers.size() >= static_cast<size_t>(n_max_outbound_peers)) {
            LOG_WARN("Maximum outbound peers reached (" + std::to_string(n_max_outbound_peers) + "), dropping random peer");

            // Find all connected outbound peers
            std::vector<size_t> connected_indices;
            for (size_t i = 0; i < m_outbound_peers.size(); i++) {
                if (m_outbound_peers[i] && m_outbound_peers[i]->IsConnected()) {
                    connected_indices.push_back(i);
                }
            }

            if (!connected_indices.empty()) {
                // Drop a random connected outbound peer
                std::srand(std::time(nullptr));
                size_t random_idx = connected_indices[std::rand() % connected_indices.size()];

                LOG_INFO("Dropping random outbound peer " + m_outbound_peers[random_idx]->GetIdentifier() +
                         " to accept new connection to " + str_address + ":" + std::to_string(n_port));

                // Store peer to disconnect outside the lock to avoid deadlock
                p_peer_to_drop = m_outbound_peers[random_idx];
            }
        }

        // Reserve a slot by adding a placeholder (nullptr)
        // This prevents other threads from exceeding n_max_outbound_peers
        // while we perform the blocking connection operation
        m_outbound_peers.push_back(nullptr);
    }

    // Disconnect peer outside the lock to avoid deadlock
    // DisconnectPeer() needs cs_peers, so we must not hold it here
    if (p_peer_to_drop) {
        DisconnectPeer(p_peer_to_drop);
    }

    // Perform blocking connection outside the lock
    bool f_success = ConnectToPeer(str_address, n_port);

    if (!f_success) {
        // Connection failed, remove the placeholder we added
        std::lock_guard<std::mutex> lock(cs_peers);
        // Remove the last nullptr entry (our placeholder)
        m_outbound_peers.erase(
            std::remove_if(m_outbound_peers.begin(), m_outbound_peers.end(),
                [](const std::shared_ptr<CPeerNode>& p) { return p == nullptr; }),
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
 * Only counts peers that have completed the VERSION/VERACK handshake.
 */
size_t CPeerManager::GetOutboundPeerCount() const {
    std::lock_guard<std::mutex> lock(cs_peers);
    size_t n_count = 0;
    for (const auto& p_peer : m_outbound_peers) {
        if (p_peer && p_peer->IsConnected()) {
            n_count++;
        }
    }
    return n_count;
}

/**
 * @brief Get count of active inbound peer connections
 * @return Number of inbound peers
 *
 * Thread-safe count using mutex lock.
 * Only counts peers that have completed the VERSION/VERACK handshake.
 */
size_t CPeerManager::GetInboundPeerCount() const {
    std::lock_guard<std::mutex> lock(cs_peers);
    size_t n_count = 0;
    for (const auto& p_peer : m_inbound_peers) {
        if (p_peer && p_peer->IsConnected()) {
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
std::vector<std::shared_ptr<CPeerNode>> CPeerManager::GetConnectedPeers() const {
    std::vector<std::shared_ptr<CPeerNode>> peers;
    std::lock_guard<std::mutex> lock(cs_peers);

    // Add inbound peers
    for (const auto& p_peer : m_inbound_peers) {
        if (p_peer && p_peer->IsConnected()) {
            peers.push_back(p_peer);
        }
    }

    // Add outbound peers
    for (const auto& p_peer : m_outbound_peers) {
        if (p_peer && p_peer->IsConnected()) {
            peers.push_back(p_peer);
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
        LOG_ERROR("Failed to serialize message of type " + message.GetType());
        return false;
    }

    // Find the peer while holding the lock
    std::shared_ptr<CPeerNode> p_peer_found;

    {
        std::lock_guard<std::mutex> lock(cs_peers);

        // Search in outbound peers
        for (const auto& p_peer : m_outbound_peers) {
            if (p_peer && p_peer->GetAddress() == str_address && p_peer->GetPort() == n_port) {
                if (p_peer->IsConnected()) {
                    p_peer_found = p_peer;
                }
                break;
            }
        }

        // If not found in outbound, search in inbound peers
        if (!p_peer_found) {
            for (const auto& p_peer : m_inbound_peers) {
                if (p_peer && p_peer->GetAddress() == str_address && p_peer->GetPort() == n_port) {
                    if (p_peer->IsConnected()) {
                        p_peer_found = p_peer;
                    }
                    break;
                }
            }
        }
    }

    // Check if peer was found
    if (!p_peer_found) {
        LOG_WARN("Cannot send message to " + str_address + ":" + std::to_string(n_port) + " - peer not connected");
        return false;
    }

    // Send the message using async I/O (works for both inbound and outbound peers)
    SendMessageAsync(p_peer_found, message);
    LOG_TRACE("Queued async " + message.GetType() + " message (" + std::to_string(str_serialized.length()) +
              " bytes) to peer " + p_peer_found->GetIdentifier());
    return true;
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
    size_t n_sent = 0;

    {
        std::lock_guard<std::mutex> lock(cs_peers);

        // Send to outbound peers using async I/O
        for (const auto& p_peer : m_outbound_peers) {
            if (p_peer && p_peer->IsConnected() && p_peer->GetSocket()) {
                SendMessageAsync(p_peer, message);
                n_sent++;
                LOG_TRACE("Queued async " + message.GetType() + " to outbound peer " +
                         p_peer->GetIdentifier());
            }
        }

        // Send to inbound peers using async I/O
        for (const auto& p_peer : m_inbound_peers) {
            if (p_peer && p_peer->IsConnected() && p_peer->GetSocket()) {
                SendMessageAsync(p_peer, message);
                n_sent++;
                LOG_TRACE("Queued async " + message.GetType() + " to inbound peer " +
                         p_peer->GetIdentifier());
            }
        }
    }

    LOG_TRACE("Broadcast complete: sent " + message.GetType() + " to " +
             std::to_string(n_sent) + " peers");

    return n_sent;
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

    // Copy peer list while holding lock, then release before sending
    // This prevents deadlock: SendMessageAsync -> HandleAsyncWrite -> DisconnectPeer also needs cs_peers
    std::vector<std::shared_ptr<CPeerNode>> peers_to_ping;
    {
        std::lock_guard<std::mutex> lock(cs_peers);
        peers_to_ping.reserve(m_outbound_peers.size() + m_inbound_peers.size());
        peers_to_ping.insert(peers_to_ping.end(), m_outbound_peers.begin(), m_outbound_peers.end());
        peers_to_ping.insert(peers_to_ping.end(), m_inbound_peers.begin(), m_inbound_peers.end());
    }

    // Send PING to all peers (without holding cs_peers lock)
    for (auto& p_peer : peers_to_ping) {
        if (p_peer && p_peer->IsConnected() && p_peer->GetSocket()) {
            // Create PING message (nonce is auto-generated)
            CPingMessage ping_message(m_n_network_magic);
            uint32_t n_nonce = ping_message.GetNonce();

            // Store nonce and send time for verification when PONG arrives
            p_peer->SetLastPingNonce(n_nonce);
            p_peer->SetLastPingSendTime(ping_send_time);

            // Send via async I/O
            SendMessageAsync(p_peer, ping_message);
            n_sent++;
            LOG_TRACE("Queued async " + ping_message.GetType() + " with nonce " + std::to_string(n_nonce) + " to peer " +
                     p_peer->GetIdentifier());
        }
    }

    LOG_INFO("Sent " + MessageType::PING + " to " + std::to_string(n_sent) + " peers");
    return n_sent;
}

// ============= Helper Methods =============

void CPeerManager::MarkInventoryKnown(std::shared_ptr<IPeerNode> p_peer_node, ObjectType::Type obj_type, const std::string& str_inventory_hash) {
    if (!p_peer_node) {
        return;  // Ignore null peer nodes
    }
    CHash hash_trace(reinterpret_cast<const unsigned char*>(str_inventory_hash.data()), str_inventory_hash.size());
    LOG_TRACE("MarkInventoryKnown: peer: " + p_peer_node->GetIdentifier() + " know object type: " + std::to_string(obj_type) + " hash: " + hash_trace.GetData());

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

void CPeerManager::BroadcastInventoryByPeerKnowledge(const std::vector<std::pair<ObjectType::Type, std::string>>& vec_inventory) {
    if (vec_inventory.empty()) {
        return;  // Nothing to broadcast
    }

    std::lock_guard<std::mutex> lock(cs_peers);
    size_t n_total_sent = 0;
    LOG_TRACE("BroadcastInventoryByPeerKnowledge: begin peer size: inbound_peers: " + std::to_string(m_inbound_peers.size()) + " outbound_peers: " + std::to_string(m_outbound_peers.size()));

    // Build inventory per peer (filtering items they already know)
    std::vector<std::shared_ptr<CPeerNode>> all_peers;
    for (auto& p_peer : m_inbound_peers) {
        if (p_peer && p_peer->IsConnected()) {
            all_peers.push_back(p_peer);
        }
    }
    for (auto& p_peer : m_outbound_peers) {
        if (p_peer && p_peer->IsConnected()) {
            all_peers.push_back(p_peer);
        }
    }

    // For each peer, build custom inventory message with items they don't know
    for (const auto& p_peer : all_peers) {
        // Filter inventory to items this peer doesn't know about
        std::vector<std::pair<ObjectType::Type, std::string>> peer_inventory;

        for (const auto& item : vec_inventory) {
            if (!PeerKnowsInventory(p_peer, item.first, item.second)) {
                peer_inventory.push_back(item);
            }
        }

        if (peer_inventory.empty()) {
            continue;  // Peer already knows everything
        }

        // Build INVENTORY message using type-safe class
        CInventoryMessage inv_message(m_n_network_magic);

        for (const auto& item : peer_inventory) {
            inv_message.AddItem(item.first, item.second);

            CHash hash_trace(reinterpret_cast<const unsigned char*>(item.second.data()), item.second.size());
            LOG_TRACE("BroadcastInventoryByPeerKnowledge: broadcasting object type: " + std::to_string(item.first) + " hash: " + hash_trace.GetData() + " to peer: " + p_peer->GetIdentifier());
            MarkInventoryKnown(p_peer, item.first, item.second);
        }

        // Send message
        SendMessageAsync(p_peer, inv_message);

        /*
        // Mark all items as known by this peer
        for (const auto& item : peer_inventory) {
            MarkInventoryKnown(p_peer, item.first, item.second);
        }
        */

        n_total_sent++;
    }

    LOG_INFO("Broadcasted INVENTORY with " + std::to_string(vec_inventory.size()) +
             " items to " + std::to_string(n_total_sent) + " peers");
}

void CPeerManager::ScheduleGetDataMessage(std::shared_ptr<CPeerNode> p_peer,
                                          const std::vector<std::pair<ObjectType::Type, std::string>>& vec_items) {
    if (vec_items.empty()) {
        return;  // Nothing to request
    }

    // Build GETDATA message using type-safe class
    CGetDataMessage getdata_message(m_n_network_magic);

    for (const auto& item : vec_items) {
        getdata_message.AddItem(item.first, item.second);

        CHash hash_trace(reinterpret_cast<const unsigned char*>(item.second.data()), item.second.size());
        LOG_TRACE("ScheduleGetDataMessage: requesting object type: " + std::to_string(item.first) + " hash: " + hash_trace.GetData());
    }

    // Send message
    SendMessageAsync(p_peer, getdata_message);

    LOG_TRACE("Sent GETDATA request for " + std::to_string(vec_items.size()) + " items");
}

// ============= Connection Rotation =============

void CPeerManager::RotateOutboundConnections() {
    int64_t now = TimeUtil::GetCurrentTime();
    int64_t elapsed = now - m_last_rotation_time;
    LOG_TRACE("RotateOutboundConnections: before rotate m_last_rotation_time: " + std::to_string(m_last_rotation_time));

    // Handle negative elapsed time (can happen when mock time is set to earlier value)
    // Treat negative elapsed as if no time has passed (reset the clock)
    if (elapsed < 0) {
        LOG_TRACE("RotateOutboundConnections: Mock time jumped backwards (elapsed: " + std::to_string(elapsed) + "), resetting rotation clock");
        m_last_rotation_time = now;
        LOG_TRACE("RotateOutboundConnections: m_last_rotation_time: " + std::to_string(m_last_rotation_time));
        return;
    }

    if (elapsed < PEER_ROTATION_INTERVAL) {
        LOG_INFO("RotateOutboundConnections: Rotating not happening: only elapsed: " + std::to_string(elapsed) + " seconds");
        return;  // Not time to rotate yet
    }
    LOG_INFO("RotateOutboundConnections: Rotating start (elapsed: " + std::to_string(elapsed) + " seconds)");

    std::vector<std::shared_ptr<CPeerNode>> peers_to_disconnect;
    {
        std::lock_guard<std::mutex> lock(cs_peers);

        if (m_outbound_peers.size() < 2) {
            return;  // Need at least 2 peers to rotate
        }

        // Find oldest 1-2 connections based on connection time
        std::vector<std::shared_ptr<CPeerNode>> oldest_peers;
        for (auto& p_peer : m_outbound_peers) {
            if (p_peer && p_peer->IsConnected()) {
                oldest_peers.push_back(p_peer);
            }
        }

        // Sort by connection time (oldest first)
        std::sort(oldest_peers.begin(), oldest_peers.end(),
                  [](const std::shared_ptr<CPeerNode>& a, const std::shared_ptr<CPeerNode>& b) {
                      return a->GetConnectionTime() < b->GetConnectionTime();
                  });

        // Store peers to disconnect
        int n_to_disconnect = std::min(2, static_cast<int>(oldest_peers.size()));
        for (int i = 0; i < n_to_disconnect; i++) {
            LOG_INFO("Rotating out outbound peer: " + oldest_peers[i]->GetIdentifier());
            peers_to_disconnect.push_back(oldest_peers[i]);
        }

        m_last_rotation_time = now;
    }
    LOG_TRACE("RotateOutboundConnections: after rotate m_last_rotation_time: " + std::to_string(m_last_rotation_time));

    // Disconnect peers outside the lock to avoid deadlock
    // DisconnectPeer() needs cs_peers, so we must not hold it here
    for (auto& p_peer : peers_to_disconnect) {
        DisconnectPeer(p_peer);
    }

    if (!peers_to_disconnect.empty()) {
        LOG_INFO("Rotated " + std::to_string(peers_to_disconnect.size()) + " outbound connections");
    }
}

bool CPeerManager::DisconnectPeerByAddress(const std::string& str_address, int n_port) {
    std::shared_ptr<CPeerNode> p_peer_to_disconnect;

    {
        std::lock_guard<std::mutex> lock(cs_peers);

        // Search outbound peers
        for (auto& p_peer : m_outbound_peers) {
            if (p_peer && p_peer->IsConnected()) {
                if (p_peer->GetAddress() == str_address && p_peer->GetPort() == n_port) {
                    p_peer_to_disconnect = p_peer;
                    break;
                }
            }
        }

        // If not found in outbound, search inbound peers
        if (!p_peer_to_disconnect) {
            for (auto& p_peer : m_inbound_peers) {
                if (p_peer && p_peer->IsConnected()) {
                    if (p_peer->GetAddress() == str_address && p_peer->GetPort() == n_port) {
                        p_peer_to_disconnect = p_peer;
                        break;
                    }
                }
            }
        }
    }

    if (p_peer_to_disconnect) {
        LOG_INFO("Disconnecting peer: " + p_peer_to_disconnect->GetIdentifier());
        DisconnectPeer(p_peer_to_disconnect);
        return true;
    }

    LOG_WARN("Peer not found: " + str_address + ":" + std::to_string(n_port));
    return false;
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
    std::vector<std::shared_ptr<CPeerNode>> peers_to_disconnect;
    {
        std::lock_guard<std::mutex> lock(cs_peers);

        // Count connections per subnet
        std::map<std::string, std::vector<std::shared_ptr<CPeerNode>>> subnet_connections;

        for (auto& p_peer : m_inbound_peers) {
            if (p_peer && p_peer->IsConnected()) {
                std::string str_subnet = GetSubnet(p_peer->GetAddress());
                subnet_connections[str_subnet].push_back(p_peer);
            }
        }

        // Disconnect one oldest connection from each subnet that has multiple connections
        // Since there is one addpeer request, just remove one oldest peer from the same subnet
        for (auto& pair : subnet_connections) {
            if (pair.second.size() > 1) {
                // Sort by connection time (oldest first)
                std::sort(pair.second.begin(), pair.second.end(),
                         [](const std::shared_ptr<CPeerNode>& a, const std::shared_ptr<CPeerNode>& b) {
                             return a->GetConnectionTime() < b->GetConnectionTime();
                         });

                // Store the oldest connection from this subnet to disconnect later
                LOG_INFO("Enforcing subnet diversity: will disconnect oldest peer from subnet " +
                         pair.first + ": " + pair.second[0]->GetIdentifier());
                peers_to_disconnect.push_back(pair.second[0]);
            }
        }
    }

    // Disconnect peers outside the lock to avoid deadlock
    // DisconnectPeer() needs cs_peers, so we must not hold it here
    for (auto& p_peer : peers_to_disconnect) {
        DisconnectPeer(p_peer);
    }

    if (!peers_to_disconnect.empty()) {
        LOG_INFO("Enforced subnet diversity: disconnected " + std::to_string(peers_to_disconnect.size()) + " peers");
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
        int64_t ban_expiry = TimeUtil::GetCurrentTime() + PEER_BAN_DURATION;
        map_banned_peers[str_peer_address] = ban_expiry;

        LOG_ERROR("Peer " + str_peer_address + " banned for " + std::to_string(PEER_BAN_DURATION) +
                 " seconds (score: " + std::to_string(n_total_score) + ")");

        // Disconnect peer
        std::lock_guard<std::mutex> peer_lock(cs_peers);
        for (auto& p_peer : m_inbound_peers) {
            if (p_peer && p_peer->GetIdentifier() == str_peer_address) {
                DisconnectPeer(p_peer);
                break;
            }
        }
        for (auto& p_peer : m_outbound_peers) {
            if (p_peer && p_peer->GetIdentifier() == str_peer_address) {
                DisconnectPeer(p_peer);
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
    int64_t now = TimeUtil::GetCurrentTime();
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

    int64_t now = TimeUtil::GetCurrentTime();
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

std::string CPeerManager::GetPublicIP() const {
    return m_str_public_ip;
}
