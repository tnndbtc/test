// ============= peer.cpp =============
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

CPeerConnection::CPeerConnection()
    : n_socket(-1), str_address(""), n_port(0), f_connected(false), f_active(false) {}

CPeerConnection::CPeerConnection(const std::string& str_addr, int n_port_num)
    : n_socket(-1), str_address(str_addr), n_port(n_port_num), f_connected(false), f_active(false) {}

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

CPeerManager::CPeerManager(int n_port)
    : n_listen_port(n_port), n_listen_socket(-1), f_running(false), f_stop_requested(false) {
    m_outbound_peers.reserve(MAX_OUTBOUND_PEERS);
}

CPeerManager::~CPeerManager() {
    Stop();
}

bool CPeerManager::Start() {
    if (f_running) {
        LOG_WARN("Peer manager already running");
        return true;
    }

    LOG_INFO("Starting peer manager on port " + std::to_string(n_listen_port));

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

    LOG_INFO("Peer Manager started on port " + std::to_string(n_listen_port));
    LOG_INFO("Maximum outbound peers: " + std::to_string(MAX_OUTBOUND_PEERS));
    LOG_INFO("Peer manager started successfully");

    return true;
}

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

bool CPeerManager::IsRunning() const {
    return f_running;
}

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

void CPeerManager::CloseListenSocket() {
    if (n_listen_socket >= 0) {
        shutdown(n_listen_socket, SHUT_RDWR);
        close(n_listen_socket);
        n_listen_socket = -1;
    }
}

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

void CPeerManager::PeerThread() {
    LOG_INFO("Peer management thread started");

    while (!f_stop_requested) {
        // Clean up disconnected peers
        CleanupDisconnectedPeers();

        // Sleep for a bit before next iteration
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    LOG_INFO("Peer management thread stopped");
}

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

size_t CPeerManager::GetOutboundPeerCount() const {
    std::lock_guard<std::mutex> lock(cs_peers);
    return m_outbound_peers.size();
}

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
