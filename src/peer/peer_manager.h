// ============= peer_manager.h =============
#ifndef PEER_MANAGER_H
#define PEER_MANAGER_H

#include "peer/i_peer_manager.h"
#include "peer/peer_message.h"
#include "peer/peer_node.h"
#include "blockcore/i_block_weave.h"
#include "utils/settings.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <map>
#include <set>
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>

/**
 * @struct PeerNodePtrComparator
 * @brief Custom comparator for shared_ptr<IPeerNode> map keys
 *
 * Compares peer node pointers by their identifier (address:port) for use as map keys.
 * This allows efficient lookup and prevents duplicate entries for the same peer.
 * Uses lexicographic comparison of the peer identifier string.
 */
struct PeerNodePtrComparator {
    bool operator()(const std::shared_ptr<IPeerNode>& lhs, const std::shared_ptr<IPeerNode>& rhs) const {
        // Handle null pointers - null is less than non-null
        if (!lhs && !rhs) return false;
        if (!lhs) return true;
        if (!rhs) return false;

        // Compare by identifier (address:port)
        return lhs->GetIdentifier() < rhs->GetIdentifier();
    }
};

/**
 * @struct CPeerConnection
 * @brief Represents a single P2P network connection
 *
 * Encapsulates a TCP socket connection to another blockweave node.
 * I/O is handled by Boost.Asio async infrastructure (not per-connection threads).
 * Supports move semantics but not copying.
 *
 * Features:
 * - Async I/O model using Boost.Asio
 * - Atomic flags for thread-safe status checking
 * - Move-only semantics for safe transfer of ownership
 * - Automatic resource cleanup in destructor
 */
struct CPeerConnection {
    int n_socket;                                                ///< Socket file descriptor (-1 if not connected)
    std::shared_ptr<CPeerNode> peer_node;                        ///< Peer node information (address and port) - shared pointer for inventory tracking
    bool f_connected;                                            ///< Current connection status
    std::atomic<bool> f_active;                                  ///< Whether connection thread is active
    uint32_t n_last_ping_nonce;                                  ///< Nonce of last sent PING message
    std::chrono::steady_clock::time_point m_last_ping_send_time; ///< Time when last PING was sent

    /**
     * @brief Default constructor - creates disconnected peer
     */
    CPeerConnection();

    /**
     * @brief Construct peer with CPeerNode
     * @param node Peer node information
     */
    CPeerConnection(const CPeerNode& node);

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
    int n_peers_ping_time;           ///< Interval in seconds between PING messages

    // Peer connections (separate inbound and outbound tracking)
    std::vector<std::unique_ptr<CPeerConnection>> m_inbound_peers;   ///< Inbound peer connections (peers connecting to us)
    std::vector<std::unique_ptr<CPeerConnection>> m_outbound_peers;  ///< Outbound peer connections (we connect to them)
    mutable std::mutex cs_peers;     ///< Mutex protecting both peer lists

    // Control flags
    std::atomic<bool> f_running;         ///< Whether peer manager is running
    std::atomic<bool> f_stop_requested;  ///< Signal to stop all threads
    std::atomic<bool> f_stop_monitor;    ///< Signal to stop monitor thread

    // Boost.Asio I/O infrastructure for inbound connections
    boost::asio::io_context m_io_context;                              ///< I/O context for async socket monitoring
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> m_io_work;  ///< Work guard to keep io_context running
    std::unique_ptr<boost::asio::thread_pool> m_thread_pool;           ///< Thread pool (MAX_INBOUND_PEERS workers) for recv/send operations
    std::map<int, std::shared_ptr<boost::asio::posix::stream_descriptor>> map_inbound_descriptors;  ///< Map of socket FD to stream descriptors for async I/O
    mutable std::mutex cs_inbound_descriptors;                         ///< Mutex protecting inbound descriptors map

    // Threads
    std::thread m_peer_thread;           ///< Main peer management thread
    std::thread m_listener_thread;       ///< Listens for inbound connections
    std::thread m_monitor_inbound_thread;///< Monitors inbound sockets via I/O multiplexing

    // PING timer
    std::chrono::steady_clock::time_point m_last_ping_time;  ///< Last time PING was sent to peers

    // Inventory broadcasting and relay
    // Map structure: peer_node -> object_type -> set of inventory hashes
    // This separates transaction and block knowledge per peer for efficient relay logic
    std::map<std::shared_ptr<IPeerNode>, std::map<ObjectType::Type, std::set<std::string>>, PeerNodePtrComparator> map_inventory_known;
    mutable std::mutex cs_inventory;                                   ///< Mutex protecting inventory tracking

    // Connection rotation
    std::chrono::steady_clock::time_point m_last_rotation_time;  ///< Last time outbound connections were rotated

    // Peer banning
    std::map<std::string, int> map_peer_misbehavior;           ///< Map of peer address -> misbehavior score
    std::map<std::string, std::chrono::steady_clock::time_point> map_banned_peers;  ///< Map of banned peer address -> ban expiry time
    mutable std::mutex cs_bans;                                 ///< Mutex protecting ban tracking

    // Blockweave integration
    IBlockweave* p_blockweave;                                  ///< Pointer to blockweave for querying mempool/blockchain

    /**
     * @brief Main peer management thread function (renamed from PeerThread)
     *
     * Periodically cleans up disconnected peers and performs
     * maintenance tasks while manager is running.
     */
    void PeerManagerThread();

    /**
     * @brief Listener thread function for accepting connections
     *
     * Accepts incoming TCP connections and registers them with
     * I/O context for async monitoring.
     */
    void ListenerThread();

    /**
     * @brief Monitor thread for inbound socket I/O multiplexing
     *
     * Runs Boost.Asio io_context event loop to monitor all inbound
     * sockets using select/epoll/poll. Dispatches recv/send work
     * to thread pool workers.
     */
    void MonitorInboundSocketThread();


    /**
     * @brief Worker thread function for processing inbound socket I/O
     * @param n_worker_id Worker thread identifier (0 to MAX_INBOUND_PEERS-1)
     *
     * Worker thread from thread pool that processes recv/send operations
     * for inbound connections dispatched by MonitorInboundSocketThread.
     */
    void InboundWorkerThread(int n_worker_id);

    /**
     * @brief Register inbound socket with async I/O context
     * @param n_socket_fd Socket file descriptor
     * @param str_address Peer IP address
     * @param n_port Peer port
     *
     * Wraps socket in Boost.Asio stream_descriptor and registers
     * async_read_some handler for non-blocking I/O monitoring.
     * Called by ListenerThread after accepting a connection.
     */
    void RegisterInboundSocket(int n_socket_fd, const std::string& str_address, int n_port);

    /**
     * @brief Register outbound socket with async I/O context
     * @param n_socket_fd Socket file descriptor
     * @param str_address Peer IP address
     * @param n_port Peer port
     *
     * Wraps socket in Boost.Asio stream_descriptor and registers
     * async_read_some handler for non-blocking I/O monitoring.
     * Called by ConnectToPeer after establishing outbound connection.
     * Uses same async I/O mechanism as inbound connections.
     */
    void RegisterOutboundSocket(int n_socket_fd, const std::string& str_address, int n_port);

    /**
     * @brief Async read completion handler for inbound sockets
     * @param ec Boost error code from async operation
     * @param n_bytes_transferred Number of bytes read
     * @param n_socket_fd Socket file descriptor
     * @param p_buffer Shared pointer to read buffer
     *
     * Called by io_context when async_read_some completes.
     * On success: Posts work to thread pool for message processing
     * On error: Handles disconnection and cleanup
     * Re-registers async_read_some for next message if still connected.
     */
    void HandleAsyncRead(const boost::system::error_code& ec,
                         size_t n_bytes_transferred,
                         int n_socket_fd,
                         std::shared_ptr<std::vector<uint8_t>> p_buffer);

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
    void ProcessReceivedMessage(int n_socket_fd,
                                std::shared_ptr<std::vector<uint8_t>> p_buffer,
                                size_t n_bytes_received);

    /**
     * @brief Send message asynchronously to inbound peer
     * @param n_socket_fd Socket file descriptor
     * @param message CPeerMessage to send
     *
     * Serializes and sends message to inbound peer using async_write.
     * Thread-safe operation. Handles errors and disconnection.
     * Used only for inbound peers (outbound peers use synchronous send in ConnectionThread).
     */
    void SendMessageAsync(int n_socket_fd, const CPeerMessage& message);

    /**
     * @brief Async write completion handler for inbound sockets
     * @param ec Boost error code from async operation
     * @param n_bytes_transferred Number of bytes written
     * @param n_socket_fd Socket file descriptor
     *
     * Called when async_write completes.
     * Handles write errors and logs completion.
     */
    void HandleAsyncWrite(const boost::system::error_code& ec,
                          size_t n_bytes_transferred,
                          int n_socket_fd);

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

    // Helper methods

    /**
     * @brief Mark inventory as known by a peer
     * @param p_peer_node Shared pointer to peer node
     * @param obj_type Object type (ObjectType::TRANSACTION or ObjectType::BLOCK)
     * @param str_inventory_hash Inventory hash
     */
    void MarkInventoryKnown(std::shared_ptr<IPeerNode> p_peer_node, ObjectType::Type obj_type, const std::string& str_inventory_hash);

    /**
     * @brief Check if peer knows about inventory
     * @param p_peer_node Shared pointer to peer node
     * @param obj_type Object type (ObjectType::TRANSACTION or ObjectType::BLOCK)
     * @param str_inventory_hash Inventory hash
     * @return true if peer knows about this inventory
     */
    bool PeerKnowsInventory(std::shared_ptr<IPeerNode> p_peer_node, ObjectType::Type obj_type, const std::string& str_inventory_hash);

    /**
     * @brief Broadcast inventory to peers who don't have it
     * @param vec_inventory Vector of (type, hash) pairs to broadcast
     *
     * Sends full inventory list to peers, filtering per-peer based on what they already know.
     * New format: [count][type][hash][type][hash]... with multiple items per message.
     */
    void BroadcastInventory(const std::vector<std::pair<ObjectType::Type, std::string>>& vec_inventory);

    /**
     * @brief Send GETDATA message to request missing inventory items
     * @param n_socket Socket to send to
     * @param vec_items Vector of (type, hash) pairs to request
     *
     * Format: [count][type][hash][type][hash]... (same as inventory format)
     */
    void SendGetDataMessage(int n_socket, const std::vector<std::pair<ObjectType::Type, std::string>>& vec_items);

    /**
     * @brief Rotate outbound peer connections
     *
     * Disconnects 1-2 oldest outbound connections and attempts
     * to establish new connections to increase network diversity.
     */
    void RotateOutboundConnections();

    /**
     * @brief Check and enforce subnet diversity for inbound connections
     *
     * Limits connections per /24 subnet (IPv4) to increase global
     * network connectivity. Rotates out excess connections from same subnet.
     */
    void EnforceSubnetDiversity();

    /**
     * @brief Increase misbehavior score for peer
     * @param str_peer_address Peer address
     * @param n_score_increase Amount to increase score by
     *
     * Tracks peer misbehavior. Bans peer if score exceeds threshold.
     */
    void IncreaseMisbehaviorScore(const std::string& str_peer_address, int n_score_increase);

    /**
     * @brief Check if peer is banned
     * @param str_peer_address Peer address
     * @return true if peer is currently banned
     */
    bool IsPeerBanned(const std::string& str_peer_address);

    /**
     * @brief Clean up expired bans
     *
     * Removes peers from ban list whose ban duration has expired.
     */
    void CleanupExpiredBans();

    /**
     * @brief Extract subnet from IP address
     * @param str_address IP address
     * @return Subnet string (e.g., "192.168.1" for /24)
     */
    std::string GetSubnet(const std::string& str_address);

public:
    /**
     * @brief Construct peer manager with listening port and max peers
     * @param n_port Port to listen on (default: P2P_PORT from settings.h)
     * @param n_max_outbound Maximum number of outbound peers (default: MAX_OUTBOUND_PEERS from settings.h)
     * @param n_max_inbound Maximum number of inbound peers (default: MAX_INBOUND_PEERS from settings.h)
     * @param n_max_workers Maximum number of inbound worker threads (default: MAX_INBOUND_WORKER_THREADS from settings.h)
     * @param n_ping_time Interval in seconds between PING messages (default: PEERS_PING_TIME from settings.h)
     */
    CPeerManager(int n_port = P2P_PORT, int n_max_outbound = MAX_OUTBOUND_PEERS, int n_max_inbound = MAX_INBOUND_PEERS, int n_max_workers = MAX_INBOUND_WORKER_THREADS, int n_ping_time = PEERS_PING_TIME);

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
     * @brief Get list of connected peer nodes
     * @return Vector of CPeerNode objects for connected peers
     *
     * Thread-safe snapshot of currently connected peers (both inbound and outbound).
     * Each CPeerNode can be queried for address, port, identifier, and info.
     */
    virtual std::vector<CPeerNode> GetConnectedPeers() const override;

    /**
     * @brief Send a message to a specific peer
     * @param str_address Peer IP address or hostname
     * @param n_port Peer listening port
     * @param message The peer message to send
     * @return true if message was sent successfully, false on error
     *
     * Sends a CPeerMessage to a specific peer node identified by address and port.
     * The message is serialized and sent over the TCP connection.
     * If no active connection exists to this peer, the send will fail.
     *
     * Thread-safe operation with mutex protection.
     */
    virtual bool SendMessageToPeer(const std::string& str_address, int n_port, const CPeerMessage& message) override;

    /**
     * @brief Broadcast a message to all connected peers
     * @param message The peer message to broadcast
     * @return Number of peers the message was successfully sent to
     *
     * Sends a CPeerMessage to all currently connected peers (both inbound and outbound).
     * The message is serialized and sent to each peer independently.
     * Failures on individual peers don't prevent sends to other peers.
     *
     * Thread-safe operation with mutex protection.
     * Failed sends are logged but don't affect the return count.
     */
    virtual size_t BroadcastMessage(const CPeerMessage& message) override;

    /**
     * @brief Send PING message to all connected peers immediately
     * @return Number of peers the PING was successfully sent to
     *
     * Sends PING messages with unique nonce to each connected peer.
     * This is used for immediate PING testing via RPC endpoint.
     *
     * Thread-safe operation with mutex protection.
     */
    virtual size_t SendPingToAllPeers() override;

    /**
     * @brief Set blockweave instance for querying mempool/blockchain
     * @param p_bw Pointer to blockweave instance
     *
     * Sets the blockweave pointer to enable smart GETDATA filtering.
     * Allows peer_manager to query whether transactions/blocks exist
     * before requesting them from peers.
     */
    void SetBlockweave(IBlockweave* p_bw);
};

#endif // PEER_MANAGER_H
