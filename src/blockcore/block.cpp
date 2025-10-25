// ============= block.cpp =============
#include "block.h"
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <ctime>

/**
 * @brief Convert epoch timestamp to human-readable UTC string
 * @param n_epoch_ns Timestamp in nanoseconds since Unix epoch
 * @return Formatted UTC timestamp string
 */
static std::string TimestampToUTC(int64_t n_epoch_ns) {
    // Convert nanoseconds to seconds for time_t
    auto seconds = n_epoch_ns / 1000000000LL;
    auto ns_remainder = n_epoch_ns % 1000000000LL;
    auto ms = ns_remainder / 1000000LL;

    std::time_t time_t = static_cast<std::time_t>(seconds);

    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms << " UTC";
    return ss.str();
}

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

void CBlock::Mine() {
    std::string str_block_data = m_previous_block.GetData() +
                                 std::to_string(m_n_height) + std::to_string(m_n_timestamp);

    for(const auto& tx : m_transactions) {
        str_block_data += tx->m_id.GetData();
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 999999);

    while(true) {
        m_str_nonce = std::to_string(dis(gen));
        m_hash = CHash(str_block_data + m_str_nonce);

        if(m_hash.GetData().substr(0, 4) < "0fff") {
            break;
        }
    }
}

std::string CBlock::ToString() const {
    std::stringstream ss;
    ss << "Block #" << m_n_height << "\n"
       << "  Hash: " << m_hash.GetData().substr(0, 16) << "...\n"
       << "  Previous: " << m_previous_block.GetData().substr(0, 16) << "...\n"
       << "  Miner: " << m_str_miner << "\n"
       << "  Transactions: " << m_transactions.size() << "\n"
       << "  Data Size: " << m_n_cumulative_data_size << " bytes\n"
       << "  Timestamp: " << m_n_timestamp << " (" << TimestampToUTC(m_n_timestamp) << ")\n";
    return ss.str();
}
