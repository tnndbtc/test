// ============= block.cpp =============
#include "block.h"
#include <chrono>
#include <random>
#include <sstream>

CBlock::CBlock(const CHash& prev_block, int64_t n_height, const std::string& str_miner)
    : m_previous_block(prev_block), m_n_height(n_height), m_str_miner(str_miner),
      m_n_difficulty(1000), m_n_cumulative_data_size(0) {
    m_n_timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    m_str_nonce = "0";
}

void CBlock::AddTransaction(std::shared_ptr<CTransaction> tx) {
    m_transactions.push_back(tx);
    m_n_cumulative_data_size += tx->m_n_data_size;
}

void CBlock::SetRecallBlock(const CHash& recall) {
    m_recall_block = recall;
}

void CBlock::Mine() {
    std::string str_block_data = m_previous_block.m_str_data + m_recall_block.m_str_data +
                                 std::to_string(m_n_height) + std::to_string(m_n_timestamp);

    for(const auto& tx : m_transactions) {
        str_block_data += tx->m_id.m_str_data;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 999999);

    while(true) {
        m_str_nonce = std::to_string(dis(gen));
        m_hash = CHash(str_block_data + m_str_nonce);

        if(m_hash.m_str_data.substr(0, 4) < "0fff") {
            break;
        }
    }
}

std::string CBlock::ToString() const {
    std::stringstream ss;
    ss << "Block #" << m_n_height << "\n"
       << "  Hash: " << m_hash.m_str_data.substr(0, 16) << "...\n"
       << "  Previous: " << m_previous_block.m_str_data.substr(0, 16) << "...\n"
       << "  Recall: " << m_recall_block.m_str_data.substr(0, 16) << "...\n"
       << "  Miner: " << m_str_miner << "\n"
       << "  Transactions: " << m_transactions.size() << "\n"
       << "  Data Size: " << m_n_cumulative_data_size << " bytes\n"
       << "  Timestamp: " << m_n_timestamp << "\n";
    return ss.str();
}
