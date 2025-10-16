// ============= blockweave.cpp =============
#include "blockweave.h"
#include "logger/logger.h"
#include <iostream>
#include <random>
#include <algorithm>

CBlockweave::CBlockweave() : f_mining_enabled(false), f_stop_mining(false) {
    m_genesis_block = std::make_shared<CBlock>(CHash(), 0, "genesis");
    m_genesis_block->Mine();

    map_blocks[m_genesis_block->m_hash.m_str_data] = m_genesis_block;
    m_block_hashes.push_back(m_genesis_block->m_hash);
    m_current_block = m_genesis_block;

    LOG_TRACE("Genesis block created!\n" + m_genesis_block->ToString());
}

CHash CBlockweave::SelectRecallBlock(int64_t n_current_height) {
    if(n_current_height <= 1) {
        return m_genesis_block->m_hash;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, m_block_hashes.size() - 1);

    int n_recall_index = dis(gen);
    return m_block_hashes[n_recall_index];
}

void CBlockweave::AddTransaction(std::shared_ptr<CTransaction> tx) {
    std::lock_guard<std::mutex> lock(cs_blockweave);
    m_mempool.push_back(tx);
    LOG_INFO("Transaction added to mempool: " + tx->m_id.m_str_data.substr(0, 16) + "...");
}

void CBlockweave::MineBlock(const std::string& str_miner_address) {
    std::lock_guard<std::mutex> lock(cs_blockweave);

    if(m_mempool.empty()) {
        return;
    }

    auto new_block = std::make_shared<CBlock>(
        m_current_block->m_hash,
        m_current_block->m_n_height + 1,
        str_miner_address
    );

    size_t n_tx_count = std::min(m_mempool.size(), size_t(10));
    for(size_t n_i = 0; n_i < n_tx_count; n_i++) {
        new_block->AddTransaction(m_mempool[n_i]);
    }
    m_mempool.erase(m_mempool.begin(), m_mempool.begin() + n_tx_count);

    CHash recall_hash = SelectRecallBlock(new_block->m_n_height);
    new_block->SetRecallBlock(recall_hash);

    LOG_INFO("Mining block #" + std::to_string(new_block->m_n_height) + " with " + std::to_string(n_tx_count) + " transactions");
    new_block->Mine();

    map_blocks[new_block->m_hash.m_str_data] = new_block;
    m_block_hashes.push_back(new_block->m_hash);
    m_current_block = new_block;

    LOG_INFO("Block #" + std::to_string(new_block->m_n_height) + " mined successfully, hash: " + new_block->m_hash.m_str_data.substr(0, 16) + "...");
}

std::shared_ptr<CBlock> CBlockweave::GetBlock(const CHash& hash) {
    std::lock_guard<std::mutex> lock(cs_blockweave);
    auto it = map_blocks.find(hash.m_str_data);
    if(it != map_blocks.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<uint8_t> CBlockweave::GetData(const CHash& tx_id) {
    std::lock_guard<std::mutex> lock(cs_blockweave);
    for(const auto& block_hash : m_block_hashes) {
        auto block = map_blocks[block_hash.m_str_data];
        for(const auto& tx : block->m_transactions) {
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
    LOG_INFO("Current height: " + std::to_string(m_current_block->m_n_height));
    LOG_INFO("Mempool size: " + std::to_string(m_mempool.size()));

    uint64_t n_total_data = 0;
    for(const auto& pair : map_blocks) {
        n_total_data += pair.second->m_n_cumulative_data_size;
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
