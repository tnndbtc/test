// ============= blockweave.cpp =============
#include "blockweave.h"
#include "logger/logger.h"
#include "peer/peer_message.h"
#include <iostream>
#include <random>
#include <algorithm>

CBlockweave::CBlockweave() : CBlockweave("data/blocks") {
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
        LOG_INFO("No existing blockchain found, creating new genesis block");

        m_genesis_block = std::make_shared<CBlock>(CHash(), 0, "genesis");
        m_genesis_block->Mine();

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
        std::lock_guard<std::mutex> lock(cs_blockweave);
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
    std::vector<std::string> transaction_ids;

    {
        std::lock_guard<std::mutex> lock(cs_blockweave);

        if(m_mempool.empty()) {
            return;
        }

        auto new_block = std::make_shared<CBlock>(
            m_current_block->GetHash(),
            m_current_block->GetHeight() + 1,
            str_miner_address
        );

        size_t n_tx_count = std::min(m_mempool.size(), size_t(10));

        // Collect transaction IDs for broadcasting
        for(size_t n_i = 0; n_i < n_tx_count; n_i++) {
            new_block->AddTransaction(m_mempool[n_i]);
            transaction_ids.push_back(m_mempool[n_i]->m_id.GetData());
        }
        m_mempool.erase(m_mempool.begin(), m_mempool.begin() + n_tx_count);

        LOG_INFO("Mining block #" + std::to_string(new_block->GetHeight()) + " with " + std::to_string(n_tx_count) + " transactions");
        new_block->Mine();

        map_blocks[new_block->GetHash().GetData()] = new_block;
        m_block_hashes.push_back(new_block->GetHash());
        m_current_block = new_block;

        // Save block to disk
        if (m_p_blockfile) {
            m_p_blockfile->SaveBlock(new_block);
        }

        LOG_INFO("Block #" + std::to_string(new_block->GetHeight()) + " mined successfully, hash: " + new_block->GetHash().GetData().substr(0, 16) + "...");
    }

    // Broadcast transaction IDs to peers (outside lock to avoid deadlock)
    if (p_peer_manager != nullptr && !transaction_ids.empty()) {
        // Create TX_IDS message with comma-separated transaction IDs
        std::string str_payload;
        for (size_t n_i = 0; n_i < transaction_ids.size(); n_i++) {
            if (n_i > 0) {
                str_payload += ",";
            }
            str_payload += transaction_ids[n_i];
        }

        CPeerMessage tx_ids_msg(MessageType::TX_IDS, str_payload);
        p_peer_manager->BroadcastMessage(tx_ids_msg);
    }
}

std::shared_ptr<CBlock> CBlockweave::GetBlock(const CHash& hash) {
    std::lock_guard<std::mutex> lock(cs_blockweave);
    auto it = map_blocks.find(hash.GetData());
    if(it != map_blocks.end()) {
        return it->second;
    }

    // Block not in memory, try loading from disk
    if (m_p_blockfile && m_p_blockfile->BlockExists(hash)) {
        LOG_TRACE("Loading block from disk: " + hash.GetData().substr(0, 16) + "...");
        auto p_block = m_p_blockfile->LoadBlock(hash);
        if (p_block) {
            // Cache in memory for future access
            map_blocks[hash.GetData()] = p_block;
            return p_block;
        }
    }

    return nullptr;
}

std::vector<uint8_t> CBlockweave::GetData(const CHash& tx_id) {
    std::lock_guard<std::mutex> lock(cs_blockweave);
    for(const auto& block_hash : m_block_hashes) {
        auto block = map_blocks[block_hash.GetData()];
        for(const auto& tx : block->GetTransactions()) {
            if(tx->m_id == tx_id) {
                return tx->m_data;
            }
        }
    }
    return {};
}

void CBlockweave::PrintChain() {
    std::lock_guard<std::mutex> lock(cs_blockweave);
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
    std::lock_guard<std::mutex> lock(cs_blockweave);
    return m_mempool.size();
}

void CBlockweave::SetPeerManager(IPeerManager* p_mgr) {
    p_peer_manager = p_mgr;
    if (p_peer_manager != nullptr) {
        LOG_INFO("Peer manager connected to blockweave");
    }
}
