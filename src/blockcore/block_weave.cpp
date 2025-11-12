// ============= block_weave.cpp =============
#include "block_weave.h"
#include "logger/logger.h"
#include "peer/peer_message.h"
#include "utils/settings.h"
#include <iostream>
#include <random>
#include <algorithm>

CBlockweave::CBlockweave() : CBlockweave("data/blocks") {
}

CBlockweave::~CBlockweave() {
}

CBlockweave::CBlockweave(const std::string& str_data_dir)
    : f_mining_enabled(false), f_stop_mining(false), p_peer_manager(nullptr),
      m_p_blockfile(std::make_unique<CBlockFile>(str_data_dir)) {

    // Try to load genesis block from disk first
    // We don't know the genesis hash yet (it depends on random mining nonce)
    // So we use GetGenesisBlock() which scans the index for a block at height 0
    auto p_loaded_genesis = m_p_blockfile ? m_p_blockfile->GetGenesisBlock() : nullptr;

    if (p_loaded_genesis) {
        // Found existing genesis on disk
        LOG_INFO("Existing blockchain detected, loading genesis from disk...");
        m_genesis_block = p_loaded_genesis;
        LOG_INFO("Genesis block loaded: " + m_genesis_block->GetHash().GetData().substr(0, 16) + "...");

        // Add genesis to in-memory structures
        map_blocks[m_genesis_block->GetHash().GetData()] = m_genesis_block;
        m_block_hashes.push_back(m_genesis_block->GetHash());
        m_current_block = m_genesis_block;

        // Note: We're only loading genesis for now
        // A full implementation would load the entire chain and rebuild state
        // For now, blocks will be loaded on-demand via GetBlock()

        LOG_INFO("Blockchain state loaded from disk");
    } else {
        // Genesis doesn't exist, create and mine a new one
        LOG_INFO("No existing blockchain found, creating genesis block");

        m_genesis_block = CBlock::CreateGenesisBlock();

        if (m_p_blockfile) {
            m_p_blockfile->SaveBlock(m_genesis_block);
        }

        map_blocks[m_genesis_block->GetHash().GetData()] = m_genesis_block;
        m_block_hashes.push_back(m_genesis_block->GetHash());
        m_current_block = m_genesis_block;

        LOG_TRACE("Genesis block created!\n" + m_genesis_block->ToString());
    }
}

void CBlockweave::AddTransaction(std::shared_ptr<CTransaction> tx) {
    std::string str_tx_id;

    {
        std::unique_lock<std::shared_mutex> lock(cs_rw_m_mempool);
        m_mempool.push_back(tx);
        str_tx_id = tx->m_id.GetData();
        LOG_INFO("Transaction added to mempool: " + str_tx_id.substr(0, 16) + "...");
    }

    // Broadcast transaction ID to peers (outside lock to avoid deadlock)
    if (p_peer_manager != nullptr && !str_tx_id.empty()) {
        CPeerMessage tx_ids_msg(MessageType::TX_IDS, str_tx_id);
        p_peer_manager->BroadcastMessage(tx_ids_msg);
    }
}

void CBlockweave::MineBlock(const std::string& str_miner_address) {
    std::string str_block_hash;
    std::shared_ptr<CBlock> new_block;

    {
        // Need to lock mempool for reading/writing
        std::unique_lock<std::shared_mutex> lock_mempool(cs_rw_m_mempool);

        if(m_mempool.empty()) {
            return;
        }

        new_block = std::make_shared<CBlock>(
            m_current_block->GetHash(),
            m_current_block->GetHeight() + 1,
            str_miner_address
        );

        size_t n_tx_count = std::min(m_mempool.size(), size_t(10));

        // Add transactions to block
        for(size_t n_i = 0; n_i < n_tx_count; n_i++) {
            new_block->AddTransaction(m_mempool[n_i]);
        }
        m_mempool.erase(m_mempool.begin(), m_mempool.begin() + n_tx_count);

        LOG_INFO("Mining block #" + std::to_string(new_block->GetHeight()) + " with " + std::to_string(n_tx_count) + " transactions");

        // Release mempool lock before mining (mining takes time)
        lock_mempool.unlock();

        new_block->Mine();

        // Lock map_blocks and m_block_hashes for writing
        std::unique_lock<std::shared_mutex> lock_blocks(cs_rw_map_blocks);
        std::unique_lock<std::shared_mutex> lock_hashes(cs_rw_m_block_hashes);

        map_blocks[new_block->GetHash().GetData()] = new_block;
        m_block_hashes.push_back(new_block->GetHash());
        m_current_block = new_block;

        lock_blocks.unlock();
        lock_hashes.unlock();

        // Save block to disk (blockfile has its own locking)
        if (m_p_blockfile) {
            m_p_blockfile->SaveBlock(new_block);
        }

        str_block_hash = new_block->GetHash().GetData();
        LOG_INFO("Block #" + std::to_string(new_block->GetHeight()) + " mined successfully, hash: " + str_block_hash.substr(0, 16) + "...");
    }

    // Broadcast block to peers via INVENTORY message (outside lock to avoid deadlock)
    if (p_peer_manager != nullptr && !str_block_hash.empty()) {
        // Build INVENTORY message manually: [count][type][hash]
        std::string str_payload;

        // Write count (4 bytes, network byte order) - we have 1 block
        uint32_t n_count = htonl(1);
        str_payload.append(reinterpret_cast<const char*>(&n_count), 4);

        // Write type (2 bytes)
        uint16_t n_type = htons(ObjectType::BLOCK);
        str_payload.append(reinterpret_cast<const char*>(&n_type), 2);

        // Write hash (32 bytes binary)
        const auto& hash_bytes = new_block->GetHash().GetBytes();
        str_payload.append(reinterpret_cast<const char*>(hash_bytes.data()), hash_bytes.size());

        // Broadcast INVENTORY message to all peers
        CPeerMessage inv_msg(MessageType::INVENTORY, str_payload);
        p_peer_manager->BroadcastMessage(inv_msg);
    }
}

std::shared_ptr<CBlock> CBlockweave::GetBlock(const CHash& hash) {
    // First try read lock for map_blocks
    {
        std::shared_lock<std::shared_mutex> lock(cs_rw_map_blocks);
        auto it = map_blocks.find(hash.GetData());
        if(it != map_blocks.end()) {
            return it->second;
        }
    }

    // Block not in memory, try loading from disk (blockfile has its own locking)
    std::shared_ptr<CBlock> p_block;
    if (m_p_blockfile && m_p_blockfile->BlockExists(hash)) {
        LOG_TRACE("Loading block from disk: " + hash.GetData().substr(0, 16) + "...");
        p_block = m_p_blockfile->LoadBlock(hash);
    }

    if (p_block) {
        // Cache in memory for future access - need write lock
        std::unique_lock<std::shared_mutex> lock(cs_rw_map_blocks);
        map_blocks[hash.GetData()] = p_block;
        return p_block;
    }

    return nullptr;
}

std::vector<uint8_t> CBlockweave::GetData(const CHash& tx_id) {
    std::shared_lock<std::shared_mutex> lock_hashes(cs_rw_m_block_hashes);
    std::shared_lock<std::shared_mutex> lock_blocks(cs_rw_map_blocks);

    for(const auto& block_hash : m_block_hashes) {
        auto block = map_blocks.at(block_hash.GetData());
        for(const auto& tx : block->GetTransactions()) {
            if(tx->m_id == tx_id) {
                return tx->m_data;
            }
        }
    }
    return {};
}

void CBlockweave::PrintChain() {
    std::shared_lock<std::shared_mutex> lock_blocks(cs_rw_map_blocks);
    std::shared_lock<std::shared_mutex> lock_mempool(cs_rw_m_mempool);

    LOG_INFO("\n=== BLOCKWEAVE STATE ===");
    LOG_INFO("Total blocks: " + std::to_string(map_blocks.size()));
    LOG_INFO("Current height: " + std::to_string(m_current_block->GetHeight()));
    LOG_INFO("Mempool size: " + std::to_string(m_mempool.size()));

    uint64_t n_total_data = 0;
    for(const auto& pair : map_blocks) {
        n_total_data += pair.second->GetCumulativeDataSize();
    }
    LOG_INFO("Total data stored: " + std::to_string(n_total_data) + " bytes");
    LOG_INFO("========================\n");
}

// Thread control methods
void CBlockweave::StartMining() {
    f_mining_enabled = true;
    f_stop_mining = false;
    LOG_INFO("Mining enabled");
}

void CBlockweave::StopMining() {
    f_stop_mining = true;
    f_mining_enabled = false;
    LOG_INFO("Mining stopped");
}

bool CBlockweave::IsMiningEnabled() const {
    return f_mining_enabled;
}

bool CBlockweave::ShouldStopMining() const {
    return f_stop_mining;
}

size_t CBlockweave::GetMempoolSize() const {
    std::shared_lock<std::shared_mutex> lock(cs_rw_m_mempool);
    return m_mempool.size();
}

size_t CBlockweave::GetBlockCount() const {
    std::shared_lock<std::shared_mutex> lock(cs_rw_map_blocks);
    return map_blocks.size();
}

bool CBlockweave::HasTransactionInMempool(const std::string& str_tx_hash) const {
    std::shared_lock<std::shared_mutex> lock(cs_rw_m_mempool);

    // Search mempool for transaction with matching hash
    for (const auto& p_tx : m_mempool) {
        if (p_tx && p_tx->m_id.GetData() == str_tx_hash) {
            return true;
        }
    }

    return false;
}

std::shared_ptr<CTransaction> CBlockweave::GetTransactionFromMempool(const CHash& tx_hash) const {
    std::shared_lock<std::shared_mutex> lock(cs_rw_m_mempool);

    // Search mempool for transaction with matching hash
    std::string str_tx_hash = tx_hash.GetData();
    for (const auto& p_tx : m_mempool) {
        if (p_tx && p_tx->m_id.GetData() == str_tx_hash) {
            return p_tx;
        }
    }

    return nullptr;  // Not found
}

void CBlockweave::SetPeerManager(IPeerManager* p_mgr) {
    p_peer_manager = p_mgr;
    if (p_peer_manager != nullptr) {
        LOG_INFO("Peer manager connected to blockweave");
    }
}

/**
 * @brief Verify and add block to blockchain
 * @param p_block Block to verify and add
 * @return pair<bool, vector<shared_ptr<CBlock>>> - success flag and vector of blocks to broadcast via INVENTORY
 *
 * This function:
 * 1. Checks if block already exists in map_blocks
 * 2. Validates block height sequencing (parent must exist)
 * 3. Validates proof-of-work
 * 4. Removes duplicate transactions from mempool
 * 5. Adds block to map_blocks
 * 6. Processes orphan blocks that were waiting for this parent
 * 7. Returns list of all blocks that should be broadcasted (input block + processed orphans)
 */
std::pair<bool, std::vector<std::shared_ptr<CBlock>>> CBlockweave::VerifyBlock(std::shared_ptr<CBlock> p_block) {
    std::vector<std::shared_ptr<CBlock>> vec_blocks_to_broadcast;

    if (!p_block) {
        LOG_WARN("VerifyBlock: null block pointer");
        return {false, vec_blocks_to_broadcast};
    }

    std::unique_lock<std::shared_mutex> lock_blocks(cs_rw_map_blocks);
    std::unique_lock<std::shared_mutex> lock_orphans(cs_rw_map_orphan_blocks);
    std::unique_lock<std::shared_mutex> lock_hashes(cs_rw_m_block_hashes);
    std::unique_lock<std::shared_mutex> lock_mempool(cs_rw_m_mempool);

    std::string str_block_hash = p_block->GetHash().GetData();
    std::string str_parent_hash = p_block->GetPreviousBlock().GetData();

    // 1. Check if block already exists in map_blocks
    if (map_blocks.find(str_block_hash) != map_blocks.end()) {
        LOG_TRACE("Block already exists, ignoring: " + str_block_hash.substr(0, 16) + "...");
        return {false, vec_blocks_to_broadcast};
    }

    // 2. Check block height sequencing - parent must exist
    auto it_parent = map_blocks.find(str_parent_hash);
    if (it_parent == map_blocks.end()) {
        // Parent not found, add to orphan blocks
        LOG_INFO("Parent block not found for block #" + std::to_string(p_block->GetHeight()) +
                 ", adding to orphan pool");

        // Enforce max orphan blocks limit
        if (map_orphan_blocks.size() >= MAX_ORPHAN_BLOCKS) {
            LOG_WARN("Orphan blocks limit reached (" + std::to_string(MAX_ORPHAN_BLOCKS) +
                     "), removing oldest orphan");
            // Remove first orphan (simple eviction policy)
            map_orphan_blocks.erase(map_orphan_blocks.begin());
        }

        map_orphan_blocks[str_block_hash] = p_block;
        LOG_INFO("Block added to orphan pool (total orphans: " +
                 std::to_string(map_orphan_blocks.size()) + ")");
        return {false, vec_blocks_to_broadcast};
    }

    std::shared_ptr<CBlock> p_parent = it_parent->second;

    // Validate height sequencing
    if (p_block->GetHeight() != p_parent->GetHeight() + 1) {
        LOG_WARN("Invalid block height: expected " + std::to_string(p_parent->GetHeight() + 1) +
                 ", got " + std::to_string(p_block->GetHeight()));
        return {false, vec_blocks_to_broadcast};
    }

    // 3. Validate proof-of-work
    // Recalculate the hash from block data and verify it matches the stored hash
    // This matches the logic in CBlock::Mine() which computes:
    //   m_hash = CHash(str_block_data + std::to_string(m_n_nonce));
    std::string str_block_data = p_block->GetPreviousBlock().GetData() +
                                 std::to_string(p_block->GetHeight()) +
                                 std::to_string(p_block->GetTimestamp());

    // Add all transaction IDs to the block data
    const auto& transactions = p_block->GetTransactions();
    for (const auto& p_tx : transactions) {
        str_block_data += p_tx->m_id.GetData();
    }

    // Recalculate hash with the nonce
    CHash calculated_hash(str_block_data + std::to_string(p_block->GetNonce()));

    // Verify the calculated hash matches the stored hash
    if (!(calculated_hash == p_block->GetHash())) {
        LOG_WARN("Invalid proof-of-work: recalculated hash does not match stored hash");
        return {false, vec_blocks_to_broadcast};
    }

    // Verify the hash meets the difficulty requirement (first 4 hex chars < "0fff")
    const std::string& str_hash = calculated_hash.GetData();
    if (str_hash.length() < 4 || str_hash.substr(0, 4) >= "0fff") {
        LOG_WARN("Invalid proof-of-work: block hash " + str_hash.substr(0, 16) +
                 "... does not meet difficulty requirement (first 4 hex chars must be < 0fff)");
        return {false, vec_blocks_to_broadcast};
    }

    LOG_INFO("Block #" + std::to_string(p_block->GetHeight()) + " passed proof-of-work validation");

    // TODO: Validate transactions in the block
    // - Check transaction signatures
    // - Verify transaction data integrity
    // - Validate spend conditions
    // This will be implemented in future updates

    // 4. Remove duplicate transactions from mempool
    const auto& block_txs = p_block->GetTransactions();
    size_t n_removed = 0;
    for (const auto& p_block_tx : block_txs) {
        auto it = std::find_if(m_mempool.begin(), m_mempool.end(),
            [&p_block_tx](const std::shared_ptr<CTransaction>& p_mempool_tx) {
                return p_mempool_tx->m_id == p_block_tx->m_id;
            });

        if (it != m_mempool.end()) {
            m_mempool.erase(it);
            n_removed++;
        }
    }

    if (n_removed > 0) {
        LOG_INFO("Removed " + std::to_string(n_removed) + " duplicate transactions from mempool");
    }

    // 5. Add block to map_blocks
    map_blocks[str_block_hash] = p_block;
    m_block_hashes.push_back(p_block->GetHash());

    // Update current block if this block has higher height
    if (p_block->GetHeight() > m_current_block->GetHeight()) {
        m_current_block = p_block;
        LOG_INFO("Updated current block to height " + std::to_string(m_current_block->GetHeight()));
    }

    // Add to broadcast list
    vec_blocks_to_broadcast.push_back(p_block);

    // Save block to disk (unlock other locks first, blockfile has its own locking)
    lock_blocks.unlock();
    lock_orphans.unlock();
    lock_hashes.unlock();
    lock_mempool.unlock();

    if (m_p_blockfile) {
        m_p_blockfile->SaveBlock(p_block);
    }

    // Re-acquire locks for orphan processing
    lock_blocks.lock();
    lock_orphans.lock();
    lock_hashes.lock();

    LOG_INFO("Block #" + std::to_string(p_block->GetHeight()) + " verified and added to blockchain");

    // 6. Scan orphan blocks to see if any need this parent
    // TODO: in a loop to detect block forks that more than one orphan is pointing to parent block
    std::vector<std::string> vec_orphans_to_process;
    for (const auto& orphan_pair : map_orphan_blocks) {
        const auto& p_orphan = orphan_pair.second;
        if (p_orphan->GetPreviousBlock().GetData() == str_block_hash) {
            vec_orphans_to_process.push_back(orphan_pair.first);
        }
    }

    // Process orphans - collect them and verify after releasing lock to avoid deadlock
    std::vector<std::shared_ptr<CBlock>> vec_orphan_blocks;
    for (const auto& str_orphan_hash : vec_orphans_to_process) {
        auto it_orphan = map_orphan_blocks.find(str_orphan_hash);
        if (it_orphan != map_orphan_blocks.end()) {
            vec_orphan_blocks.push_back(it_orphan->second);
            map_orphan_blocks.erase(it_orphan);
            LOG_INFO("Removed orphan block from pool, will verify now");
        }
    }

    // Note: We're still holding the lock, so we need to inline the verification
    // to avoid recursive mutex locking. We'll process orphans iteratively.
    for (const auto& p_orphan : vec_orphan_blocks) {
        LOG_INFO("Processing orphan block #" + std::to_string(p_orphan->GetHeight()) +
                 " (parent now available)");

        std::string str_orphan_hash = p_orphan->GetHash().GetData();
        std::string str_orphan_parent = p_orphan->GetPreviousBlock().GetData();

        // Inline verification (already holding lock)
        // Check if parent exists now
        auto it_orphan_parent = map_blocks.find(str_orphan_parent);
        if (it_orphan_parent != map_blocks.end()) {
            // Add to blockchain
            map_blocks[str_orphan_hash] = p_orphan;
            m_block_hashes.push_back(p_orphan->GetHash());

            // Update current block if needed
            if (p_orphan->GetHeight() > m_current_block->GetHeight()) {
                m_current_block = p_orphan;
            }

            // Add to broadcast list
            vec_blocks_to_broadcast.push_back(p_orphan);

            // Save to disk (blockfile has its own locking - unlock first to avoid holding locks during I/O)
            lock_blocks.unlock();
            lock_orphans.unlock();
            lock_hashes.unlock();

            if (m_p_blockfile) {
                m_p_blockfile->SaveBlock(p_orphan);
            }

            // Re-acquire locks
            lock_blocks.lock();
            lock_orphans.lock();
            lock_hashes.lock();

            LOG_INFO("Orphan block #" + std::to_string(p_orphan->GetHeight()) + " verified and added");
        } else {
            // Parent still missing, re-add to orphan pool
            LOG_INFO("Orphan block parent still missing, re-adding to orphan pool");
            map_orphan_blocks[str_orphan_hash] = p_orphan;
        }
    }

    return {true, vec_blocks_to_broadcast};
}
