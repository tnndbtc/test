// ============= i_peer.h =============
#ifndef I_PEER_H
#define I_PEER_H

#include <string>
#include <vector>

// Forward declaration to avoid circular dependency
class CPeerMessage;

/**
 * @interface IPeerManager
 * @brief Interface for peer-to-peer network management
 *
 * Defines the contract that all peer manager implementations must follow.
 * Provides abstraction layer for P2P networking, allowing different
 * implementations and easier testing through mocking.
 *
 * Key responsibilities:
 * - Starting/stopping P2P network services
 * - Managing peer connections
 * - Broadcasting messages to peers
 * - Providing network status information
 *
 * Example usage:
 *   IPeerManager* p_peer_mgr = new CPeerManager(1984);
 *   p_peer_mgr->Start();
 *   p_peer_mgr->AddPeer("192.168.1.100", 1984);
 *   p_peer_mgr->BroadcastTransactionIds(tx_ids);
 *   p_peer_mgr->Stop();
 *   delete p_peer_mgr;
 */
class IPeerManager {
public:
    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~IPeerManager() = default;

    /**
     * @brief Start peer manager and networking threads
     * @return true if started successfully, false on error
     *
     * Initializes the P2P network, creates listening socket,
     * and starts background threads for peer management.
     */
    virtual bool Start() = 0;

    /**
     * @brief Stop peer manager and all networking
     *
     * Signals all threads to stop, closes connections, joins threads.
     * Blocks until all threads have terminated.
     */
    virtual void Stop() = 0;

    /**
     * @brief Check if peer manager is running
     * @return true if running, false otherwise
     *
     * Thread-safe check of peer manager state.
     */
    virtual bool IsRunning() const = 0;

    /**
     * @brief Add outbound connection to peer
     * @param str_address Peer IP address or hostname
     * @param n_port Peer listening port
     * @return true if connection initiated successfully
     *
     * Attempts to establish connection to specified peer.
     * Connection happens asynchronously in background thread.
     */
    virtual bool AddPeer(const std::string& str_address, int n_port) = 0;

    /**
     * @brief Get count of active outbound peer connections
     * @return Number of outbound peers
     *
     * Thread-safe count of peers in outbound peer list.
     */
    virtual size_t GetOutboundPeerCount() const = 0;

    /**
     * @brief Get count of active inbound peer connections
     * @return Number of inbound peers
     *
     * Thread-safe count of peers in inbound peer list.
     */
    virtual size_t GetInboundPeerCount() const = 0;

    /**
     * @brief Get list of connected peer addresses
     * @return Vector of "address:port" strings for connected peers
     *
     * Thread-safe snapshot of currently connected peers.
     */
    virtual std::vector<std::string> GetConnectedPeers() const = 0;

    /**
     * @brief Broadcast transaction IDs to all connected peers
     * @param transaction_ids Vector of transaction ID strings to broadcast
     *
     * Sends transaction IDs to all active outbound peers.
     * Non-blocking operation that sends to each peer independently.
     * Failed sends are logged but don't affect other peers.
     */
    virtual void BroadcastTransactionIds(const std::vector<std::string>& transaction_ids) = 0;

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
    virtual bool SendMessageToPeer(const std::string& str_address, int n_port, const CPeerMessage& message) = 0;

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
    virtual size_t BroadcastMessage(const CPeerMessage& message) = 0;
};

#endif // I_PEER_H
