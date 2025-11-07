// ============= block.cpp =============
#include "block.h"
#include "utils/settings.h"
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstring>

// Platform-specific byte order conversion
#ifdef __APPLE__
    #include <machine/endian.h>
    #include <libkern/OSByteOrder.h>
    #define htobe64(x) OSSwapHostToBigInt64(x)
    #define be64toh(x) OSSwapBigToHostInt64(x)
#elif defined(__linux__)
    #include <endian.h>
    #include <arpa/inet.h>
#elif defined(_WIN32)
    #include <winsock2.h>
    #define htobe64(x) htonll(x)
    #define be64toh(x) ntohll(x)
#endif

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

std::string CBlock::Serialize() const {
    std::string str_result;

    // Write hash (32 bytes binary)
    const auto& hash_bytes = m_hash.GetBytes();
    str_result.append(reinterpret_cast<const char*>(hash_bytes.data()), hash_bytes.size());

    // Write previous block hash (32 bytes binary)
    const auto& prev_hash_bytes = m_previous_block.GetBytes();
    str_result.append(reinterpret_cast<const char*>(prev_hash_bytes.data()), prev_hash_bytes.size());

    // Write height (BLOCK_INT64_SIZE bytes, network byte order)
    uint64_t n_height_network = htobe64(static_cast<uint64_t>(m_n_height));
    str_result.append(reinterpret_cast<const char*>(&n_height_network), BLOCK_INT64_SIZE);

    // Write timestamp (BLOCK_INT64_SIZE bytes, network byte order)
    uint64_t n_timestamp_network = htobe64(static_cast<uint64_t>(m_n_timestamp));
    str_result.append(reinterpret_cast<const char*>(&n_timestamp_network), BLOCK_INT64_SIZE);

    // Write miner (length + data)
    uint32_t n_miner_len = htonl(static_cast<uint32_t>(m_str_miner.length()));
    str_result.append(reinterpret_cast<const char*>(&n_miner_len), BLOCK_UINT32_SIZE);
    str_result.append(m_str_miner);

    // Write difficulty (BLOCK_UINT64_SIZE bytes, network byte order)
    uint64_t n_difficulty_network = htobe64(m_n_difficulty);
    str_result.append(reinterpret_cast<const char*>(&n_difficulty_network), BLOCK_UINT64_SIZE);

    // Write cumulative data size (BLOCK_UINT64_SIZE bytes, network byte order)
    uint64_t n_cumulative_data_size_network = htobe64(m_n_cumulative_data_size);
    str_result.append(reinterpret_cast<const char*>(&n_cumulative_data_size_network), BLOCK_UINT64_SIZE);

    // Write nonce (length + data)
    uint32_t n_nonce_len = htonl(static_cast<uint32_t>(m_str_nonce.length()));
    str_result.append(reinterpret_cast<const char*>(&n_nonce_len), BLOCK_UINT32_SIZE);
    str_result.append(m_str_nonce);

    // Write transaction count
    uint32_t n_tx_count = htonl(static_cast<uint32_t>(m_transactions.size()));
    str_result.append(reinterpret_cast<const char*>(&n_tx_count), BLOCK_UINT32_SIZE);

    // Write each transaction
    for (const auto& p_tx : m_transactions) {
        std::string str_tx_data = p_tx->Serialize();
        uint32_t n_tx_len = htonl(static_cast<uint32_t>(str_tx_data.length()));
        str_result.append(reinterpret_cast<const char*>(&n_tx_len), BLOCK_UINT32_SIZE);
        str_result.append(str_tx_data);
    }

    return str_result;
}

std::shared_ptr<CBlock> CBlock::Deserialize(const std::string& str_data) {
    size_t n_offset = 0;

    // Minimum size check (hash + prev_hash + height + timestamp + miner_len + difficulty + cumulative + nonce_len + tx_count)
    const size_t min_size = BLOCK_HASH_SIZE + BLOCK_HASH_SIZE + BLOCK_INT64_SIZE + BLOCK_INT64_SIZE +
                            BLOCK_UINT32_SIZE + BLOCK_UINT64_SIZE + BLOCK_UINT64_SIZE + BLOCK_UINT32_SIZE + BLOCK_UINT32_SIZE;
    if (str_data.length() < min_size) {
        return nullptr;  // Invalid data
    }

    try {
        // Read hash
        CHash hash(reinterpret_cast<const unsigned char*>(str_data.data() + n_offset), BLOCK_HASH_SIZE);
        n_offset += BLOCK_HASH_SIZE;

        // Read previous block hash
        CHash prev_hash(reinterpret_cast<const unsigned char*>(str_data.data() + n_offset), BLOCK_HASH_SIZE);
        n_offset += BLOCK_HASH_SIZE;

        // Read height
        uint64_t n_height_network;
        std::memcpy(&n_height_network, str_data.data() + n_offset, BLOCK_INT64_SIZE);
        int64_t n_height = static_cast<int64_t>(be64toh(n_height_network));
        n_offset += BLOCK_INT64_SIZE;

        // Read timestamp
        uint64_t n_timestamp_network;
        std::memcpy(&n_timestamp_network, str_data.data() + n_offset, BLOCK_INT64_SIZE);
        int64_t n_timestamp = static_cast<int64_t>(be64toh(n_timestamp_network));
        n_offset += BLOCK_INT64_SIZE;

        // Read miner length
        uint32_t n_miner_len_network;
        std::memcpy(&n_miner_len_network, str_data.data() + n_offset, BLOCK_UINT32_SIZE);
        uint32_t n_miner_len = ntohl(n_miner_len_network);
        n_offset += BLOCK_UINT32_SIZE;

        // Read miner
        if (n_offset + n_miner_len > str_data.length()) return nullptr;
        std::string str_miner(str_data.data() + n_offset, n_miner_len);
        n_offset += n_miner_len;

        // Create block object
        auto p_block = std::make_shared<CBlock>(prev_hash, n_height, str_miner);
        p_block->m_hash = hash;
        p_block->m_n_timestamp = n_timestamp;

        // Read difficulty
        uint64_t n_difficulty_network;
        std::memcpy(&n_difficulty_network, str_data.data() + n_offset, BLOCK_UINT64_SIZE);
        p_block->m_n_difficulty = be64toh(n_difficulty_network);
        n_offset += BLOCK_UINT64_SIZE;

        // Read cumulative data size
        uint64_t n_cumulative_data_size_network;
        std::memcpy(&n_cumulative_data_size_network, str_data.data() + n_offset, BLOCK_UINT64_SIZE);
        p_block->m_n_cumulative_data_size = be64toh(n_cumulative_data_size_network);
        n_offset += BLOCK_UINT64_SIZE;

        // Read nonce length
        uint32_t n_nonce_len_network;
        std::memcpy(&n_nonce_len_network, str_data.data() + n_offset, BLOCK_UINT32_SIZE);
        uint32_t n_nonce_len = ntohl(n_nonce_len_network);
        n_offset += BLOCK_UINT32_SIZE;

        // Read nonce
        if (n_offset + n_nonce_len > str_data.length()) return nullptr;
        p_block->m_str_nonce = std::string(str_data.data() + n_offset, n_nonce_len);
        n_offset += n_nonce_len;

        // Read transaction count
        uint32_t n_tx_count_network;
        std::memcpy(&n_tx_count_network, str_data.data() + n_offset, BLOCK_UINT32_SIZE);
        uint32_t n_tx_count = ntohl(n_tx_count_network);
        n_offset += BLOCK_UINT32_SIZE;

        // Read each transaction
        for (uint32_t i = 0; i < n_tx_count; ++i) {
            // Read transaction length
            if (n_offset + BLOCK_UINT32_SIZE > str_data.length()) return nullptr;
            uint32_t n_tx_len_network;
            std::memcpy(&n_tx_len_network, str_data.data() + n_offset, BLOCK_UINT32_SIZE);
            uint32_t n_tx_len = ntohl(n_tx_len_network);
            n_offset += BLOCK_UINT32_SIZE;

            // Read transaction data
            if (n_offset + n_tx_len > str_data.length()) return nullptr;
            std::string str_tx_data(str_data.data() + n_offset, n_tx_len);
            n_offset += n_tx_len;

            // Deserialize transaction
            auto p_tx = CTransaction::Deserialize(str_tx_data);
            if (!p_tx) return nullptr;

            p_block->m_transactions.push_back(p_tx);
        }

        return p_block;
    } catch (...) {
        return nullptr;  // Deserialization error
    }
}
