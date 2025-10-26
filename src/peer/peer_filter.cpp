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

CPeerFilter::CPeerFilter() {
}

// ============= Public Methods =============

/**
 * @brief Record that a peer knows about a transaction
 */
void CPeerFilter::AddTxIdForPeer(const std::string& str_tx_id, const std::string& str_address, int n_port) {
    std::lock_guard<std::mutex> lock(cs_filter);
    std::string str_peer = GetPeerIdentifier(str_address, n_port);
    map_tx_peers[str_tx_id].insert(str_peer);
}

/**
 * @brief Record that a peer knows about a block
 */
void CPeerFilter::AddBlockForPeer(const std::string& str_block_hash, const std::string& str_address, int n_port) {
    std::lock_guard<std::mutex> lock(cs_filter);
    std::string str_peer = GetPeerIdentifier(str_address, n_port);
    map_block_peers[str_block_hash].insert(str_peer);
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
