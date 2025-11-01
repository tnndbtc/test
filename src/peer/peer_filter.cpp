// ============= peer_filter.cpp =============
#include "peer_filter.h"
#include <sstream>

// ============= Helper Functions =============

/**
 * @brief Create peer identifier string from address and port
 */
std::string CPeerFilter::GetPeerIdentifier(const std::string& str_address, int n_port) {
    std::ostringstream oss;
    oss << str_address << ":" << n_port;
    return oss.str();
}

// ============= Constructor =============

CPeerFilter::CPeerFilter() : m_last_cleanup_time(std::chrono::steady_clock::now()) {
}

// ============= Public Methods =============

/**
 * @brief Record that a peer knows about a transaction
 */
void CPeerFilter::AddTxIdForPeer(const std::string& str_tx_id, const std::string& str_address, int n_port) {
    std::lock_guard<std::mutex> lock(cs_filter);
    std::string str_peer = GetPeerIdentifier(str_address, n_port);
    map_tx_peers[str_tx_id].insert(str_peer);
    map_tx_timestamps[str_tx_id] = std::chrono::steady_clock::now();

    // Periodic cleanup (every 60 seconds)
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_last_cleanup_time).count();
    if (elapsed >= 60) {
        CleanupOldEntries();
        m_last_cleanup_time = now;
    }
}

/**
 * @brief Record that a peer knows about a block
 */
void CPeerFilter::AddBlockForPeer(const std::string& str_block_hash, const std::string& str_address, int n_port) {
    std::lock_guard<std::mutex> lock(cs_filter);
    std::string str_peer = GetPeerIdentifier(str_address, n_port);
    map_block_peers[str_block_hash].insert(str_peer);
    map_block_timestamps[str_block_hash] = std::chrono::steady_clock::now();

    // Periodic cleanup (every 60 seconds)
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_last_cleanup_time).count();
    if (elapsed >= 60) {
        CleanupOldEntries();
        m_last_cleanup_time = now;
    }
}

/**
 * @brief Check if a peer already knows about a transaction
 */
bool CPeerFilter::PeerKnowsTxId(const std::string& str_tx_id, const std::string& str_address, int n_port) const {
    std::lock_guard<std::mutex> lock(cs_filter);

    auto it = map_tx_peers.find(str_tx_id);
    if (it == map_tx_peers.end()) {
        return false;
    }

    std::string str_peer = GetPeerIdentifier(str_address, n_port);
    return it->second.find(str_peer) != it->second.end();
}

/**
 * @brief Check if a peer already knows about a block
 */
bool CPeerFilter::PeerKnowsBlock(const std::string& str_block_hash, const std::string& str_address, int n_port) const {
    std::lock_guard<std::mutex> lock(cs_filter);

    auto it = map_block_peers.find(str_block_hash);
    if (it == map_block_peers.end()) {
        return false;
    }

    std::string str_peer = GetPeerIdentifier(str_address, n_port);
    return it->second.find(str_peer) != it->second.end();
}

/**
 * @brief Get list of peers that don't know about a transaction
 */
std::vector<std::string> CPeerFilter::GetPeersWithoutTxId(const std::string& str_tx_id,
                                                          const std::vector<std::string>& vec_all_peers) const {
    std::lock_guard<std::mutex> lock(cs_filter);

    std::vector<std::string> vec_filtered_peers;

    // Get set of peers that already know about this TX_ID
    auto it = map_tx_peers.find(str_tx_id);
    const std::set<std::string>* p_known_peers = (it != map_tx_peers.end()) ? &it->second : nullptr;

    // Filter out peers that already know
    for (const auto& str_peer : vec_all_peers) {
        if (p_known_peers == nullptr || p_known_peers->find(str_peer) == p_known_peers->end()) {
            vec_filtered_peers.push_back(str_peer);
        }
    }

    return vec_filtered_peers;
}

/**
 * @brief Get list of peers that don't know about a block
 */
std::vector<std::string> CPeerFilter::GetPeersWithoutBlock(const std::string& str_block_hash,
                                                           const std::vector<std::string>& vec_all_peers) const {
    std::lock_guard<std::mutex> lock(cs_filter);

    std::vector<std::string> vec_filtered_peers;

    // Get set of peers that already know about this block
    auto it = map_block_peers.find(str_block_hash);
    const std::set<std::string>* p_known_peers = (it != map_block_peers.end()) ? &it->second : nullptr;

    // Filter out peers that already know
    for (const auto& str_peer : vec_all_peers) {
        if (p_known_peers == nullptr || p_known_peers->find(str_peer) == p_known_peers->end()) {
            vec_filtered_peers.push_back(str_peer);
        }
    }

    return vec_filtered_peers;
}

/**
 * @brief Remove a transaction ID from tracking
 */
void CPeerFilter::RemoveTxId(const std::string& str_tx_id) {
    std::lock_guard<std::mutex> lock(cs_filter);
    map_tx_peers.erase(str_tx_id);
}

/**
 * @brief Remove a block hash from tracking
 */
void CPeerFilter::RemoveBlock(const std::string& str_block_hash) {
    std::lock_guard<std::mutex> lock(cs_filter);
    map_block_peers.erase(str_block_hash);
}

/**
 * @brief Clear all tracked data
 */
void CPeerFilter::Clear() {
    std::lock_guard<std::mutex> lock(cs_filter);
    map_tx_peers.clear();
    map_block_peers.clear();
}

/**
 * @brief Get count of tracked transaction IDs
 */
size_t CPeerFilter::GetTxIdCount() const {
    std::lock_guard<std::mutex> lock(cs_filter);
    return map_tx_peers.size();
}

/**
 * @brief Get count of tracked block hashes
 */
size_t CPeerFilter::GetBlockCount() const {
    std::lock_guard<std::mutex> lock(cs_filter);
    return map_block_peers.size();
}

// ============= Private Methods =============

/**
 * @brief Remove old entries based on timestamp (RollingBloomFilter-style)
 *
 * Implements probabilistic eviction:
 * - Removes entries older than n_entry_lifetime_seconds (10 minutes)
 * - If still over capacity, removes oldest entries
 * - Target sizes: 50,000 TXs, 5,000 blocks
 *
 * Must be called with cs_filter lock already held.
 */
void CPeerFilter::CleanupOldEntries() {
    auto now = std::chrono::steady_clock::now();
    auto max_age = std::chrono::seconds(n_entry_lifetime_seconds);

    // Clean up old transaction IDs
    std::vector<std::string> tx_to_remove;
    for (const auto& pair : map_tx_timestamps) {
        auto age = now - pair.second;
        if (age > max_age) {
            tx_to_remove.push_back(pair.first);
        }
    }

    for (const auto& tx_id : tx_to_remove) {
        map_tx_peers.erase(tx_id);
        map_tx_timestamps.erase(tx_id);
    }

    // If still over capacity, remove oldest entries
    if (map_tx_peers.size() > n_max_tx_ids) {
        // Sort by timestamp (oldest first)
        std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> sorted_txs(
            map_tx_timestamps.begin(), map_tx_timestamps.end());
        std::sort(sorted_txs.begin(), sorted_txs.end(),
                 [](const auto& a, const auto& b) { return a.second < b.second; });

        // Remove oldest entries to get under capacity
        size_t n_to_remove = map_tx_peers.size() - n_max_tx_ids;
        for (size_t i = 0; i < n_to_remove && i < sorted_txs.size(); i++) {
            map_tx_peers.erase(sorted_txs[i].first);
            map_tx_timestamps.erase(sorted_txs[i].first);
        }
    }

    // Clean up old block hashes
    std::vector<std::string> blocks_to_remove;
    for (const auto& pair : map_block_timestamps) {
        auto age = now - pair.second;
        if (age > max_age) {
            blocks_to_remove.push_back(pair.first);
        }
    }

    for (const auto& block_hash : blocks_to_remove) {
        map_block_peers.erase(block_hash);
        map_block_timestamps.erase(block_hash);
    }

    // If still over capacity, remove oldest entries
    if (map_block_peers.size() > n_max_block_hashes) {
        // Sort by timestamp (oldest first)
        std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> sorted_blocks(
            map_block_timestamps.begin(), map_block_timestamps.end());
        std::sort(sorted_blocks.begin(), sorted_blocks.end(),
                 [](const auto& a, const auto& b) { return a.second < b.second; });

        // Remove oldest entries to get under capacity
        size_t n_to_remove = map_block_peers.size() - n_max_block_hashes;
        for (size_t i = 0; i < n_to_remove && i < sorted_blocks.size(); i++) {
            map_block_peers.erase(sorted_blocks[i].first);
            map_block_timestamps.erase(sorted_blocks[i].first);
        }
    }
}
