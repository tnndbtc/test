// ============= peer.h =============
#ifndef PEER_H
#define PEER_H

#include "utils/settings.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

// Represents a single peer connection
struct CPeerConnection {
    int n_socket;                    // Socket file descriptor
    std::string str_address;         // Peer IP address
    int n_port;                      // Peer port
    bool f_connected;                // Connection status
    std::atomic<bool> f_active;      // Whether connection is active
    std::thread m_thread;            // Thread handling this connection

    CPeerConnection();
    CPeerConnection(const std::string& str_addr, int n_port_num);
    ~CPeerConnection();

    // Disable copy constructor and assignment
    CPeerConnection(const CPeerConnection&) = delete;
    CPeerConnection& operator=(const CPeerConnection&) = delete;

    // Enable move constructor and assignment
    CPeerConnection(CPeerConnection&& other) noexcept;
    CPeerConnection& operator=(CPeerConnection&& other) noexcept;
};

// Manages peer-to-peer network connections
class CPeerManager {
private:
    // Network configuration
    int n_listen_port;
    int n_listen_socket;

    // Peer connections
    std::vector<std::unique_ptr<CPeerConnection>> m_outbound_peers;
    mutable std::mutex cs_peers;

    // Control flags
    std::atomic<bool> f_running;
    std::atomic<bool> f_stop_requested;

    // Threads
    std::thread m_peer_thread;       // Main peer management thread
    std::thread m_listener_thread;   // Listens for inbound connections

    // Thread functions
    void PeerThread();
    void ListenerThread();
    void ConnectionThread(CPeerConnection* p_peer);

    // Connection management
    bool ConnectToPeer(const std::string& str_address, int n_port);
    void DisconnectPeer(CPeerConnection* p_peer);
    void CleanupDisconnectedPeers();

    // Socket utilities
    bool CreateListenSocket();
    void CloseListenSocket();
    bool SetSocketKeepAlive(int n_socket);
    bool SetSocketNonBlocking(int n_socket, bool f_non_blocking);

public:
    CPeerManager(int n_port = P2P_PORT);
    ~CPeerManager();

    // Control methods
    bool Start();
    void Stop();
    bool IsRunning() const;

    // Peer management
    bool AddPeer(const std::string& str_address, int n_port = P2P_PORT);
    size_t GetOutboundPeerCount() const;
    std::vector<std::string> GetConnectedPeers() const;
};

#endif // PEER_H
