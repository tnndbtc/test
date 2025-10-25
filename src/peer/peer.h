// ============= peer.h =============
#ifndef PEER_H
#define PEER_H

#include "peer/i_peer.h"
#include "utils/settings.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

/**
 * @struct CPeerConnection
 * @brief Represents a single P2P network connection
 *
 * Encapsulates a TCP socket connection to another blockweave node.
 * Each connection runs in its own thread for asynchronous communication.
 * Supports move semantics but not copying (non-copyable due to thread member).
 *
 * Features:
 * - Thread-per-connection model
 * - Atomic flags for thread-safe status checking
 * - Move-only semantics for safe transfer of ownership
 * - Automatic resource cleanup in destructor
 */
struct CPeerConnection {
    int n_socket;                    ///< Socket file descriptor (-1 if not connected)
    std::string str_address;         ///< Peer IP address or hostname
    int n_port;                      ///< Peer listening port
    bool f_connected;                ///< Current connection status
    std::atomic<bool> f_active;      ///< Whether connection thread is active
    std::thread m_thread;            ///< Thread handling this connection

    /**
     * @brief Default constructor - creates disconnected peer
     */
    CPeerConnection();

    /**
     * @brief Construct peer with address and port
     * @param str_addr Peer IP address or hostname
     * @param n_port_num Peer listening port
     */
    CPeerConnection(const std::string& str_addr, int n_port_num);

    /**
     * @brief Destructor - closes connection and joins thread
     */
    ~CPeerConnection();

    /**
     * @brief Deleted copy constructor (non-copyable due to thread)
     */
    CPeerConnection(const CPeerConnection&) = delete;

    /**
     * @brief Deleted copy assignment (non-copyable due to thread)
     */
    CPeerConnection& operator=(const CPeerConnection&) = delete;

    /**
     * @brief Move constructor for transferring ownership
     * @param other Peer connection to move from
     */
    CPeerConnection(CPeerConnection&& other) noexcept;

    /**
     * @brief Move assignment for transferring ownership
     * @param other Peer connection to move from
     * @return Reference to this
     */
    CPeerConnection& operator=(CPeerConnection&& other) noexcept;
};

/**
 * @class CPeerManager
 * @brief Manages peer-to-peer network connections for blockweave node
 *
 * Implements IPeerManager interface to provide P2P networking functionality.
 *
 * Handles all P2P networking including:
 * - Listening for inbound connections
 * - Managing outbound connections to peers
 * - Thread-safe peer lifecycle management
 * - Socket configuration (keep-alive, non-blocking)
 *
 * Architecture:
 * - Listener thread accepts incoming connections
 * - Peer thread manages periodic tasks
 * - Each connection runs in its own thread
 * - Mutex-protected peer list for thread safety
 *
 * Example usage:
 *   CPeerManager peer_mgr(1984);
 *   peer_mgr.Start();
 *   peer_mgr.AddPeer("192.168.1.100", 1984);
 *   // ... networking happens in background threads ...
 *   peer_mgr.Stop();
 */
class CPeerManager : public IPeerManager {
private:
    // Network configuration
    int n_listen_port;               ///< Port for listening to incoming connections
    int n_listen_socket;             ///< Server socket file descriptor
    int n_max_inbound_peers;         ///< Maximum number of inbound peer connections
    int n_max_outbound_peers;        ///< Maximum number of outbound peer connections

    // Peer connections (separate inbound and outbound tracking)
    std::vector<std::unique_ptr<CPeerConnection>> m_inbound_peers;   ///< Inbound peer connections (peers connecting to us)
    std::vector<std::unique_ptr<CPeerConnection>> m_outbound_peers;  ///< Outbound peer connections (we connect to them)
    mutable std::mutex cs_peers;     ///< Mutex protecting both peer lists

    // Control flags
    std::atomic<bool> f_running;         ///< Whether peer manager is running
    std::atomic<bool> f_stop_requested;  ///< Signal to stop all threads

    // Threads
    std::thread m_peer_thread;       ///< Main peer management thread
    std::thread m_listener_thread;   ///< Listens for inbound connections

    /**
     * @brief Main peer management thread function
     *
     * Periodically cleans up disconnected peers and performs
     * maintenance tasks while manager is running.
     */
    void PeerThread();

    /**
     * @brief Listener thread function for accepting connections
     *
     * Accepts incoming TCP connections and spawns connection
     * threads to handle them.
     */
    void ListenerThread();

    /**
     * @brief Connection thread function for individual peer
     * @param p_peer Pointer to peer connection to handle
     *
     * Handles communication with a single peer in dedicated thread.
     * Runs until connection is closed or stop requested.
     */
    void ConnectionThread(CPeerConnection* p_peer);

    /**
     * @brief Initiate outbound connection to peer
     * @param str_address Peer IP address or hostname
     * @param n_port Peer listening port
     * @return true if connection established, false on error
     *
     * Creates socket, connects to peer, and spawns connection thread.
     */
    bool ConnectToPeer(const std::string& str_address, int n_port);

    /**
     * @brief Disconnect from peer and cleanup resources
     * @param p_peer Pointer to peer connection to disconnect
     *
     * Closes socket, stops thread, and marks peer inactive.
     */
    void DisconnectPeer(CPeerConnection* p_peer);

    /**
     * @brief Remove disconnected peers from peer list
     *
     * Thread-safe cleanup of peers that are no longer connected.
     * Called periodically by peer management thread.
     */
    void CleanupDisconnectedPeers();

    /**
     * @brief Create and bind listening socket
     * @return true if socket created and bound successfully
     *
     * Sets up TCP server socket with SO_REUSEADDR option.
     */
    bool CreateListenSocket();

    /**
     * @brief Close listening socket
     *
     * Shuts down server socket, causing accept() to fail
     * and listener thread to exit.
     */
    void CloseListenSocket();

    /**
     * @brief Enable TCP keep-alive on socket
     * @param n_socket Socket file descriptor
     * @return true if keep-alive enabled successfully
     *
     * Configures socket to send periodic keep-alive probes
     * to detect dead connections.
     */
    bool SetSocketKeepAlive(int n_socket);

    /**
     * @brief Set socket blocking/non-blocking mode
     * @param n_socket Socket file descriptor
     * @param f_non_blocking true for non-blocking, false for blocking
     * @return true if mode set successfully
     */
    bool SetSocketNonBlocking(int n_socket, bool f_non_blocking);

public:
    /**
     * @brief Construct peer manager with listening port and max peers
     * @param n_port Port to listen on (default: P2P_PORT from settings.h)
     * @param n_max_outbound Maximum number of outbound peers (default: MAX_OUTBOUND_PEERS from settings.h)
     * @param n_max_inbound Maximum number of inbound peers (default: MAX_INBOUND_PEERS from settings.h)
     */
    CPeerManager(int n_port = P2P_PORT, int n_max_outbound = MAX_OUTBOUND_PEERS, int n_max_inbound = MAX_INBOUND_PEERS);

    /**
     * @brief Destructor - stops networking and cleans up resources
     */
    virtual ~CPeerManager() override;

    // IPeerManager interface implementation

    /**
     * @brief Start peer manager and networking threads
     * @return true if started successfully, false on error
     *
     * Creates listening socket and starts listener and peer threads.
     */
    virtual bool Start() override;

    /**
     * @brief Stop peer manager and all networking
     *
     * Signals all threads to stop, closes connections, joins threads.
     * Blocks until all threads have terminated.
     */
    virtual void Stop() override;

    /**
     * @brief Check if peer manager is running
     * @return true if running, false otherwise
     */
    virtual bool IsRunning() const override;

    /**
     * @brief Add outbound connection to peer
     * @param str_address Peer IP address or hostname
     * @param n_port Peer listening port (default: P2P_PORT)
     * @return true if connection initiated successfully
     *
     * Attempts to establish connection to specified peer.
     * Connection happens asynchronously in background thread.
     */
    virtual bool AddPeer(const std::string& str_address, int n_port = P2P_PORT) override;

    /**
     * @brief Get count of active outbound peer connections
     * @return Number of outbound peers
     *
     * Thread-safe count of peers in outbound peer list.
     */
    virtual size_t GetOutboundPeerCount() const override;

    /**
     * @brief Get count of active inbound peer connections
     * @return Number of inbound peers
     *
     * Thread-safe count of peers in inbound peer list.
     */
    virtual size_t GetInboundPeerCount() const override;

    /**
     * @brief Get list of connected peer addresses
     * @return Vector of "address:port" strings for connected peers
     *
     * Thread-safe snapshot of currently connected peers (both inbound and outbound).
     */
    virtual std::vector<std::string> GetConnectedPeers() const override;

    /**
     * @brief Broadcast transaction IDs to all connected peers
     * @param transaction_ids Vector of transaction ID strings to broadcast
     *
     * Sends transaction IDs to all active outbound peers.
     * Non-blocking operation that sends to each peer independently.
     * Failed sends are logged but don't affect other peers.
     */
    virtual void BroadcastTransactionIds(const std::vector<std::string>& transaction_ids) override;
};

#endif // PEER_H
