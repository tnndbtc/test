// ============= peer_filter.h =============
#ifndef PEER_FILTER_H
#define PEER_FILTER_H

#include <string>
#include <vector>
#include <set>
#include <map>
#include <mutex>
#include <chrono>

/**
 * @class CPeerFilter
 * @brief Tracks which peers have knowledge of transactions and blocks
 *
 * Prevents redundant broadcasts by maintaining a record of which peers
 * have already sent or received specific TX_IDS and BLOCK messages.
 * This allows the node to filter out peers during broadcasts, avoiding
 * sending duplicate information.
 *
 * Thread-safe implementation with mutex protection for concurrent access.
 *
 * Example usage:
 *   CPeerFilter filter;
 *
 *   // Record that peer knows about a transaction
 *   filter.AddTxIdForPeer("tx_abc123", "192.168.1.100", 8333);
 *
 *   // Check if peer already knows
 *   if (filter.PeerKnowsTxId("tx_abc123", "192.168.1.100", 8333)) {
 *       // Skip sending to this peer
 *   }
 *
 *   // Get peers that don't know about a transaction
 *   auto all_peers = peer_manager->GetConnectedPeers();
 *   auto filtered_peers = filter.GetPeersWithoutTxId("tx_abc123", all_peers);
 *   // Broadcast only to filtered_peers
 */
class CPeerFilter {
private:
    mutable std::mutex cs_filter;                              ///< Mutex for thread-safe access
    std::map<std::string, std::set<std::string>> map_tx_peers; ///< TX_ID -> set of peer "address:port"
    std::map<std::string, std::set<std::string>> map_block_peers; ///< Block hash -> set of peer "address:port"

    // RollingBloomFilter-style time-based eviction
    std::map<std::string, std::chrono::steady_clock::time_point> map_tx_timestamps;    ///< TX_ID -> insertion time
    std::map<std::string, std::chrono::steady_clock::time_point> map_block_timestamps; ///< Block hash -> insertion time
    std::chrono::steady_clock::time_point m_last_cleanup_time; ///< Last time cleanup was performed

    // Size targets (RollingBloomFilter-style limits)
    const size_t n_max_tx_ids = 50000;     ///< Max transaction IDs (10k-50k target)
    const size_t n_max_block_hashes = 5000; ///< Max block hashes (1k-5k target)
    const int n_entry_lifetime_seconds = 600; ///< Entry lifetime (10 minutes)

    /**
     * @brief Helper to create peer identifier string
     * @param str_address Peer IP address or hostname
     * @param n_port Peer port number
     * @return Peer identifier in format "address:port"
     */
    static std::string GetPeerIdentifier(const std::string& str_address, int n_port);

    /**
     * @brief Remove old entries based on timestamp (probabilistic eviction)
     *
     * Implements RollingBloomFilter-style cleanup:
     * - Removes entries older than n_entry_lifetime_seconds
     * - If still over capacity, removes oldest entries
     * - Should be called periodically during Add operations
     */
    void CleanupOldEntries();

public:
    /**
     * @brief Default constructor
     */
    CPeerFilter();

    /**
     * @brief Destructor
     */
    ~CPeerFilter() = default;

    /**
     * @brief Record that a peer knows about a transaction
     * @param str_tx_id Transaction ID
     * @param str_address Peer IP address or hostname
     * @param n_port Peer port number
     *
     * Thread-safe operation. Adds the peer to the set of peers that know
     * about this transaction ID.
     */
    void AddTxIdForPeer(const std::string& str_tx_id, const std::string& str_address, int n_port);

    /**
     * @brief Record that a peer knows about a block
     * @param str_block_hash Block hash
     * @param str_address Peer IP address or hostname
     * @param n_port Peer port number
     *
     * Thread-safe operation. Adds the peer to the set of peers that know
     * about this block.
     */
    void AddBlockForPeer(const std::string& str_block_hash, const std::string& str_address, int n_port);

    /**
     * @brief Check if a peer already knows about a transaction
     * @param str_tx_id Transaction ID
     * @param str_address Peer IP address or hostname
     * @param n_port Peer port number
     * @return true if peer knows about this transaction, false otherwise
     *
     * Thread-safe operation.
     */
    bool PeerKnowsTxId(const std::string& str_tx_id, const std::string& str_address, int n_port) const;

    /**
     * @brief Check if a peer already knows about a block
     * @param str_block_hash Block hash
     * @param str_address Peer IP address or hostname
     * @param n_port Peer port number
     * @return true if peer knows about this block, false otherwise
     *
     * Thread-safe operation.
     */
    bool PeerKnowsBlock(const std::string& str_block_hash, const std::string& str_address, int n_port) const;

    /**
     * @brief Get list of peers that don't know about a transaction
     * @param str_tx_id Transaction ID
     * @param vec_all_peers List of all peer identifiers ("address:port")
     * @return Vector of peer identifiers that don't know about this transaction
     *
     * Filters the input peer list to return only peers that haven't
     * been recorded as knowing about this transaction. Useful for
     * targeted broadcasting.
     *
     * Thread-safe operation.
     */
    std::vector<std::string> GetPeersWithoutTxId(const std::string& str_tx_id,
                                                  const std::vector<std::string>& vec_all_peers) const;

    /**
     * @brief Get list of peers that don't know about a block
     * @param str_block_hash Block hash
     * @param vec_all_peers List of all peer identifiers ("address:port")
     * @return Vector of peer identifiers that don't know about this block
     *
     * Filters the input peer list to return only peers that haven't
     * been recorded as knowing about this block. Useful for
     * targeted broadcasting.
     *
     * Thread-safe operation.
     */
    std::vector<std::string> GetPeersWithoutBlock(const std::string& str_block_hash,
                                                   const std::vector<std::string>& vec_all_peers) const;

    /**
     * @brief Remove a transaction ID from tracking
     * @param str_tx_id Transaction ID to remove
     *
     * Removes the transaction ID and all associated peer records.
     * Useful for cleanup after transactions are confirmed in blocks.
     *
     * Thread-safe operation.
     */
    void RemoveTxId(const std::string& str_tx_id);

    /**
     * @brief Remove a block hash from tracking
     * @param str_block_hash Block hash to remove
     *
     * Removes the block hash and all associated peer records.
     * Useful for cleanup of old blocks.
     *
     * Thread-safe operation.
     */
    void RemoveBlock(const std::string& str_block_hash);

    /**
     * @brief Clear all tracked data
     *
     * Removes all transaction and block tracking data.
     * Thread-safe operation.
     */
    void Clear();

    /**
     * @brief Get count of tracked transaction IDs
     * @return Number of unique transaction IDs being tracked
     *
     * Thread-safe operation.
     */
    size_t GetTxIdCount() const;

    /**
     * @brief Get count of tracked block hashes
     * @return Number of unique block hashes being tracked
     *
     * Thread-safe operation.
     */
    size_t GetBlockCount() const;
};

#endif // PEER_FILTER_H
