// ============= peer_manager.h =============
#ifndef PEER_MANAGER_H
#define PEER_MANAGER_H

#include "peer/i_peer_manager.h"
#include "peer/peer_message.h"
#include "peer/peer_node.h"
#include "blockcore/i_block_weave.h"
#include "utils/settings.h"
#include "utils/network.h"
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
#include <boost/asio/ip/tcp.hpp>

// Note: Platform-specific async types have been unified to always use tcp::socket.
// Previous implementation used stream_descriptor on POSIX for wrapping raw FDs,
// but now Boost.Asio manages socket creation throughout for better type safety.

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
    // Boost.Asio I/O infrastructure for all connections (inbound and outbound)
    // IMPORTANT: These MUST be declared BEFORE m_p_acceptor to ensure correct destruction order!
    // Destruction happens in reverse order: acceptor destructs first, then io_context.
    // If io_context destructs first, acceptor destructor will access destroyed io_context internals.
    boost::asio::io_context m_io_context;                              ///< I/O context for async socket monitoring
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> m_io_work;  ///< Work guard to keep io_context running
    std::unique_ptr<boost::asio::thread_pool> m_thread_pool;           ///< Thread pool (MAX_INBOUND_PEERS workers) for message processing

    // Network configuration
    int n_listen_port;               ///< Port for listening to incoming connections
    std::string str_bind_ip;         ///< IP address to bind to (empty means all interfaces)
    std::unique_ptr<boost::asio::ip::tcp::acceptor> m_p_acceptor;  ///< Boost.Asio acceptor for async accept (destructs BEFORE io_context)
    int n_max_inbound_peers;         ///< Maximum number of inbound peer connections
    int n_max_outbound_peers;        ///< Maximum number of outbound peer connections
    int n_peers_ping_time;           ///< Interval in seconds between PING messages
    uint32_t m_n_network_magic;      ///< Network magic bytes for P2P message validation

    // Peer connections (separate inbound and outbound tracking)
    std::vector<std::shared_ptr<CPeerNode>> m_inbound_peers;   ///< Inbound peer connections (peers connecting to us)
    std::vector<std::shared_ptr<CPeerNode>> m_outbound_peers;  ///< Outbound peer connections (we connect to them)
    mutable std::mutex cs_peers;     ///< Mutex protecting both peer lists

    // Control flags
    std::atomic<bool> f_running;         ///< Whether peer manager is running
    std::atomic<bool> f_stop_requested;  ///< Signal to stop all threads
    std::atomic<bool> f_stop_monitor;    ///< Signal to stop monitor thread

    // Threads
    std::thread m_peer_thread;           ///< Main peer management thread
    std::thread m_monitor_inbound_thread;///< Monitors inbound sockets via I/O multiplexing and async accept

    // PING timer
    int64_t m_last_ping_time;  ///< Last time PING was sent to peers (UNIX timestamp in seconds)

    // Inventory broadcasting and relay
    // Map structure: peer_node -> object_type -> set of inventory hashes
    // This separates transaction and block knowledge per peer for efficient relay logic
    std::map<std::shared_ptr<IPeerNode>, std::map<ObjectType::Type, std::set<std::string>>, PeerNodePtrComparator> map_inventory_known;
    mutable std::mutex cs_inventory;                                   ///< Mutex protecting inventory tracking

    // Connection rotation
    int64_t m_last_rotation_time;  ///< Last time outbound connections were rotated (UNIX timestamp in seconds)

    // Peer banning
    std::map<std::shared_ptr<IPeerNode>, int, PeerNodePtrComparator> map_peer_misbehavior;           ///< Map of peer node -> misbehavior score
    std::map<std::shared_ptr<IPeerNode>, int64_t, PeerNodePtrComparator> map_banned_peers;  ///< Map of banned peer node -> ban expiry time (UNIX timestamp in seconds)
    mutable std::mutex cs_bans;                                 ///< Mutex protecting ban tracking

    // Blockweave integration
    std::shared_ptr<IBlockweave> p_blockweave;                  ///< Shared pointer to blockweave for querying mempool/blockchain

    // Public IP discovery
    std::string m_str_public_ip;                                ///< Current public IP address (default: "127.0.0.1")
    std::vector<std::string> m_vec_stun_addresses;               ///< STUN server addresses for periodic IP updates
    int64_t m_last_public_ip_check;///< Last time public IP was checked (UNIX timestamp in seconds)

    // Protocol support
    uint64_t m_n_services;                                      ///< 64-bit bitmap of services (e.g., NODE_NETWORK)

    /**
     * @brief Main peer management thread function (renamed from PeerThread)
     *
     * Periodically cleans up disconnected peers and performs
     * maintenance tasks while manager is running.
     */
    void PeerManagerThread();

    /**
     * @brief Start async accept chain for incoming connections
     *
     * Initiates async_accept on the acceptor. Each successful accept
     * triggers a callback that processes the connection and chains
     * the next async_accept.
     */
    void StartAccept();

    /**
     * @brief Async accept completion handler
     * @param ec Error code from async_accept
     * @param socket Accepted socket (moved into callback)
     *
     * Callback invoked when async_accept completes. Processes the
     * new connection (ban checking, peer limits, registration) and
     * chains the next async_accept.
     */
    void HandleAccept(const boost::system::error_code& ec, boost::asio::ip::tcp::socket socket);

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
     * @brief Register inbound/outbound socket with async I/O context
     * @param p_peer Shared pointer to peer node
     *
     * Registers async_read_some handler for non-blocking I/O monitoring.
     * Called by HandleAccept after accepting a connection.
     */
    void RegisterSocket(std::shared_ptr<CPeerNode> p_peer);

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
    // void RegisterOutboundSocket(int n_socket_fd, const std::string& str_address, int n_port);

    /**
     * @brief Async read completion handler for inbound sockets
     * @param ec Boost error code from async operation
     * @param n_bytes_transferred Number of bytes read
     * @param p_peer Shared pointer to peer node
     * @param p_buffer Shared pointer to read buffer
     *
     * Called by io_context when async_read_some completes.
     * On success: Posts work to thread pool for message processing
     * On error: Handles disconnection and cleanup
     * Re-registers async_read_some for next message if still connected.
     */
    void HandleAsyncRead(const boost::system::error_code& ec,
                         size_t n_bytes_transferred,
                         std::shared_ptr<CPeerNode> p_peer,
                         std::shared_ptr<std::vector<uint8_t>> p_buffer);

    /**
     * @brief Process received message from inbound peer
     * @param p_peer Shared pointer to peer node
     * @param p_buffer Shared pointer to buffer containing received data
     * @param n_bytes_received Number of bytes in buffer
     *
     * Processes messages from inbound async connections.
     * Handles PING, PONG, and other message types.
     * Posted to thread pool for parallel processing.
     */
    void ProcessReceivedMessage(std::shared_ptr<CPeerNode> p_peer,
                                std::shared_ptr<std::vector<uint8_t>> p_buffer,
                                size_t n_bytes_received);

    /**
     * @brief Handle PING message from peer
     * @param p_peer_shared Shared pointer to peer (used for async operations)
     * @param received_msg The received PING message
     */
    void HandlePingMessage(std::shared_ptr<CPeerNode> p_peer_shared,
                          const CPeerMessage& received_msg);

    /**
     * @brief Handle PONG message from peer
     * @param p_peer Peer connection pointer
     * @param received_msg The received PONG message
     */
    void HandlePongMessage(std::shared_ptr<CPeerNode> p_peer_shared, const CPeerMessage& received_msg);

    /**
     * @brief Handle INVENTORY message from peer
     * @param p_peer_shared Shared pointer to peer (used for async operations)
     * @param received_msg The received INVENTORY message
     */
    void HandleInventoryMessage(std::shared_ptr<CPeerNode> p_peer_shared,
                               const CPeerMessage& received_msg);

    /**
     * @brief Handle VERSION message from peer
     * @param p_peer Peer connection pointer
     * @param received_msg The received VERSION message
     */
    void HandleVersionMessage(std::shared_ptr<CPeerNode> p_peer_shared, const CPeerMessage& received_msg);

    /**
     * @brief Handle VERACK message from peer
     * @param p_peer_shared Shared pointer to peer
     * @param received_msg The received VERACK message
     */
    void HandleVerackMessage(std::shared_ptr<CPeerNode> p_peer_shared, const CPeerMessage& received_msg);

    /**
     * @brief Handle GETDATA message from peer
     * @param p_peer_shared Shared pointer to peer (used for async operations)
     * @param received_msg The received GETDATA message
     */
    void HandleGetDataMessage(std::shared_ptr<CPeerNode> p_peer_shared,
                             const CPeerMessage& received_msg);

    /**
     * @brief Handle TX message from peer
     * @param p_peer Peer connection pointer
     * @param received_msg The received TX message
     */
    void HandleTxMessage(std::shared_ptr<CPeerNode> p_peer_shared, const CPeerMessage& received_msg);

    /**
     * @brief Handle BLOCK message from peer
     * @param p_peer Peer connection pointer
     * @param received_msg The received BLOCK message
     */
    void HandleBlockMessage(std::shared_ptr<CPeerNode> p_peer_shared, const CPeerMessage& received_msg);

    /**
     * @brief Send message asynchronously to inbound peer
     * @param p_peer Peer node to send message to
     * @param message CPeerMessage to send
     *
     * Serializes and sends message to inbound peer using async_write.
     * Thread-safe operation. Handles errors and disconnection.
     * Used only for inbound peers (outbound peers use synchronous send in ConnectionThread).
     */
    void SendMessageAsync(std::shared_ptr<CPeerNode> p_peer,
                          const CPeerMessage& message);

    /**
     * @brief Async write completion handler for inbound sockets
     * @param ec Boost error code from async operation
     * @param n_bytes_transferred Number of bytes written
     * @param p_peer Peer node for this connection
     *
     * Called when async_write completes.
     * Handles write errors and logs completion.
     */
    void HandleAsyncWrite(const boost::system::error_code& ec,
                          size_t n_bytes_transferred,
                          std::shared_ptr<CPeerNode> p_peer);

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
    void DisconnectPeer(std::shared_ptr<CPeerNode> p_peer_shared);

    /**
     * @brief Create and configure Boost.Asio acceptor
     * @return true if acceptor created and bound successfully
     *
     * Sets up Boost.Asio TCP acceptor with SO_REUSEADDR and SO_REUSEPORT options.
     */
    bool CreateAcceptor();

    /**
     * @brief Close acceptor and stop accepting connections
     *
     * Cancels any pending async_accept operations and closes the acceptor.
     */
    void CloseAcceptor();

    /**
     * @brief Enable TCP keep-alive on socket
     * @param n_socket Socket file descriptor
     * @return true if keep-alive enabled successfully
     *
     * Configures socket to send periodic keep-alive probes
     * to detect dead connections.
     */
    bool SetSocketKeepAlive(std::shared_ptr<boost::asio::ip::tcp::socket> p_socket);

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
    void BroadcastInventoryByPeerKnowledge(const std::vector<std::pair<ObjectType::Type, std::string>>& vec_inventory) override;

    /**
     * @brief Schedule GETDATA message to request missing inventory items
     * @param p_peer Peer to send message to
     * @param vec_items Vector of (type, hash) pairs to request
     *
     * Format: [count][type][hash][type][hash]... (same as inventory format)
     * Note: This method schedules the GETDATA message to be sent asynchronously
     *       via the thread pool to avoid blocking the caller.
     */
    void ScheduleGetDataMessage(std::shared_ptr<CPeerNode> p_peer,
                                const std::vector<std::pair<ObjectType::Type, std::string>>& vec_items);

    /**
     * @brief Check and enforce subnet diversity for inbound connections
     *
     * Limits connections per /24 subnet (IPv4) to increase global
     * network connectivity. Rotates out excess connections from same subnet.
     */
    void EnforceSubnetDiversity();

    /**
     * @brief Increase misbehavior score for peer
     * @param p_peer_node Shared pointer to peer node
     * @param n_score_increase Amount to increase score by
     *
     * Tracks peer misbehavior. Bans peer if score exceeds threshold.
     */
    void IncreaseMisbehaviorScore(std::shared_ptr<IPeerNode> p_peer_node, int n_score_increase);

    /**
     * @brief Check if peer is banned
     * @param p_peer_node Shared pointer to peer node
     * @return true if peer is currently banned
     */
    bool IsPeerBanned(std::shared_ptr<IPeerNode> p_peer_node);

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
     * @param n_magic Network magic bytes for P2P message validation (default: MAINNET_MAGIC from settings.h)
     * @param str_bind Bind IP address for P2P listening (default: BIND_IP from settings.h, empty means all interfaces)
     */
    CPeerManager(int n_port = P2P_PORT, int n_max_outbound = MAX_OUTBOUND_PEERS, int n_max_inbound = MAX_INBOUND_PEERS, int n_max_workers = MAX_INBOUND_WORKER_THREADS, int n_ping_time = PEERS_PING_TIME, uint32_t n_magic = LOCALNET_MAGIC, const std::string& str_bind = BIND_IP);

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
    virtual std::vector<std::shared_ptr<CPeerNode>> GetConnectedPeers() const override;

    /**
     * @brief Rotate outbound peer connections
     *
     * Disconnects 1-2 oldest outbound connections and attempts
     * to establish new connections to increase network diversity.
     *
     * This is exposed publicly for testing purposes (via /rpc/triggerrotation endpoint).
     * In normal operation, this is called automatically by PeerManagerThread every 30 minutes.
     */
    void RotateOutboundConnections();

    /**
     * @brief Disconnect a specific peer by address and port
     * @param str_address Peer IP address
     * @param n_port Peer port
     * @return true if peer was found and disconnected, false otherwise
     *
     * This is exposed publicly for testing purposes (via /rpc/disconnectpeer endpoint).
     * Localnet only.
     */
    bool DisconnectPeerByAddress(const std::string& str_address, int n_port);

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
     * @param p_bw Shared pointer to blockweave instance
     *
     * Sets the blockweave shared pointer to enable smart GETDATA filtering.
     * Allows peer_manager to query whether transactions/blocks exist
     * before requesting them from peers. Uses shared_ptr for safe ownership.
     */
    void SetBlockweave(std::shared_ptr<IBlockweave> p_bw);

    /**
     * @brief Get current public IP address
     * @return Public IP as string (e.g., "203.0.113.42" or "127.0.0.1")
     *
     * Returns the public IP address discovered via STUN, manually configured,
     * or "127.0.0.1" if in LOCALNET mode or discovery failed.
     */
    std::string GetPublicIP() const;
};

#endif // PEER_MANAGER_H
