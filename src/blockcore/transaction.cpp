// ============= transaction.cpp =============
/**
 * @file transaction.cpp
 * @brief Implementation of blockweave transaction
 *
 * Provides transaction construction with automatic ID generation
 * and timestamping for data storage operations.
 */

#include "transaction.h"
#include "utils/settings.h"
#include "utils/hash.h"
#include <chrono>
#include <cstring>
#include <sstream>

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

namespace {
    /**
     * @brief Escape special characters for JSON string values
     * @param str String to escape
     * @return Escaped string safe for JSON
     *
     * Escapes quotes, backslashes, and control characters to ensure
     * valid JSON output.
     */
    std::string EscapeJsonString(const std::string& str) {
        std::ostringstream oss;
        for (char c : str) {
            switch (c) {
                case '\"': oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;
                default: oss << c; break;
            }
        }
        return oss.str();
    }

    /**
     * @brief Convert TransactionType enum to string
     * @param type Transaction type enum value
     * @return String representation of transaction type
     */
    std::string TransactionTypeToString(TransactionType type) {
        switch (type) {
            case TransactionType::TRANSFER: return "TRANSFER";
            default: return "UNKNOWN";
        }
    }
}

/**
 * @brief Construct transaction with automatic ID and timestamp generation
 * @param str_owner Address of transaction creator/sender
 * @param str_target Address of transaction recipient
 * @param data Binary data payload to store
 * @param n_reward Mining reward/fee for including this transaction
 *
 * Initializes all transaction fields:
 * - Copies owner, target, data, and reward from parameters
 * - Caches data size for quick access
 * - Generates timestamp from system clock (nanoseconds since epoch)
 * - Computes unique transaction ID as SHA-256 hash of:
 *   owner + target + data + reward + timestamp + type
 *
 * Including all fields in the hash ensures data integrity and prevents
 * transaction malleability attacks.
 */
CTransaction::CTransaction(const std::string& str_owner, const std::string& str_target,
                           const std::vector<uint8_t>& data, uint64_t n_reward)
    : m_id(),
      m_str_owner(str_owner),
      m_str_target(str_target),
      m_data(),
      m_n_data_size(data.size()),
      m_n_reward(n_reward),
      m_n_timestamp(0),
      m_type(TransactionType::TRANSFER),
      m_str_metadata() {
    // Copy vector data using assign to avoid container-overflow detection
    m_data.assign(data.begin(), data.end());

    m_n_timestamp = std::chrono::system_clock::now().time_since_epoch().count();

    // Compute transaction ID from all fields to ensure data integrity
    std::string str_id_input = str_owner + str_target;
    str_id_input.insert(str_id_input.end(), m_data.begin(), m_data.end());
    str_id_input += std::to_string(n_reward);
    str_id_input += std::to_string(m_n_timestamp);
    str_id_input += std::to_string(static_cast<uint8_t>(m_type));

    m_id = CHash::ComputeSHA256(str_id_input);
}

/**
 * @brief Construct transaction with type and metadata
 * @param str_owner Address of transaction creator/sender
 * @param str_target Address of transaction recipient
 * @param data Binary data payload to store
 * @param n_reward Mining reward/fee for including this transaction
 * @param type Transaction type (currently only TRANSFER is supported)
 * @param str_meta Service-specific metadata in JSON format
 *
 * Initializes transaction with specified type and metadata.
 */
CTransaction::CTransaction(const std::string& str_owner, const std::string& str_target,
                           const std::vector<uint8_t>& data, uint64_t n_reward,
                           TransactionType type, const std::string& str_meta)
    : m_id(),
      m_str_owner(str_owner),
      m_str_target(str_target),
      m_data(),
      m_n_data_size(data.size()),
      m_n_reward(n_reward),
      m_n_timestamp(0),
      m_type(type),
      m_str_metadata(str_meta) {
    // Copy vector data using assign to avoid container-overflow detection
    m_data.assign(data.begin(), data.end());

    m_n_timestamp = std::chrono::system_clock::now().time_since_epoch().count();

    // Compute transaction ID from all fields and metadata
    std::string str_id_input = str_owner + str_target;
    str_id_input.insert(str_id_input.end(), m_data.begin(), m_data.end());
    str_id_input += std::to_string(n_reward);
    str_id_input += std::to_string(m_n_timestamp);
    str_id_input += std::to_string(static_cast<uint8_t>(m_type));
    str_id_input += str_meta;

    m_id = CHash::ComputeSHA256(str_id_input);
}

std::string CTransaction::Serialize() const {
    // Calculate total size
    size_t total_size = BLOCK_UINT32_SIZE + m_str_owner.length() +
                        BLOCK_UINT32_SIZE + m_str_target.length() +
                        BLOCK_UINT32_SIZE + m_data.size() +
                        BLOCK_UINT64_SIZE + BLOCK_INT64_SIZE + BLOCK_UINT8_SIZE +
                        BLOCK_UINT32_SIZE + m_str_metadata.length();

    std::string str_result;
    str_result.resize(total_size);
    size_t offset = 0;

    // Write owner (length + data)
    uint32_t n_owner_len = htonl(static_cast<uint32_t>(m_str_owner.length()));
    std::memcpy(&str_result[offset], &n_owner_len, BLOCK_UINT32_SIZE);
    offset += BLOCK_UINT32_SIZE;
    std::memcpy(&str_result[offset], m_str_owner.data(), m_str_owner.length());
    offset += m_str_owner.length();

    // Write target (length + data)
    uint32_t n_target_len = htonl(static_cast<uint32_t>(m_str_target.length()));
    std::memcpy(&str_result[offset], &n_target_len, BLOCK_UINT32_SIZE);
    offset += BLOCK_UINT32_SIZE;
    std::memcpy(&str_result[offset], m_str_target.data(), m_str_target.length());
    offset += m_str_target.length();

    // Write data (length + data)
    uint32_t n_data_len = htonl(static_cast<uint32_t>(m_data.size()));
    std::memcpy(&str_result[offset], &n_data_len, BLOCK_UINT32_SIZE);
    offset += BLOCK_UINT32_SIZE;
    std::memcpy(&str_result[offset], m_data.data(), m_data.size());
    offset += m_data.size();

    // Write reward (BLOCK_UINT64_SIZE bytes, network byte order)
    uint64_t n_reward_network = htobe64(m_n_reward);
    std::memcpy(&str_result[offset], &n_reward_network, BLOCK_UINT64_SIZE);
    offset += BLOCK_UINT64_SIZE;

    // Write timestamp (BLOCK_INT64_SIZE bytes, network byte order)
    uint64_t n_timestamp_network = htobe64(static_cast<uint64_t>(m_n_timestamp));
    std::memcpy(&str_result[offset], &n_timestamp_network, BLOCK_INT64_SIZE);
    offset += BLOCK_INT64_SIZE;

    // Write type (BLOCK_UINT8_SIZE byte)
    uint8_t n_type = static_cast<uint8_t>(m_type);
    std::memcpy(&str_result[offset], &n_type, BLOCK_UINT8_SIZE);
    offset += BLOCK_UINT8_SIZE;

    // Write metadata (length + data)
    uint32_t n_metadata_len = htonl(static_cast<uint32_t>(m_str_metadata.length()));
    std::memcpy(&str_result[offset], &n_metadata_len, BLOCK_UINT32_SIZE);
    offset += BLOCK_UINT32_SIZE;
    std::memcpy(&str_result[offset], m_str_metadata.data(), m_str_metadata.length());
    offset += m_str_metadata.length();

    return str_result;
}

std::shared_ptr<CTransaction> CTransaction::Deserialize(const std::string& str_data) {
    size_t n_offset = 0;

    // Minimum size check (owner_len + target_len + data_len + reward + timestamp + type + metadata_len)
    const size_t min_size = BLOCK_UINT32_SIZE + BLOCK_UINT32_SIZE + BLOCK_UINT32_SIZE +
                            BLOCK_UINT64_SIZE + BLOCK_INT64_SIZE + BLOCK_UINT8_SIZE + BLOCK_UINT32_SIZE;
    if (str_data.length() < min_size) {
        return nullptr;  // Invalid data
    }

    try {
        // Read owner length
        uint32_t n_owner_len_network;
        std::memcpy(&n_owner_len_network, str_data.data() + n_offset, BLOCK_UINT32_SIZE);
        uint32_t n_owner_len = ntohl(n_owner_len_network);
        n_offset += BLOCK_UINT32_SIZE;

        // Read owner
        if (n_offset + n_owner_len > str_data.length()) return nullptr;
        std::string str_owner(str_data.data() + n_offset, n_owner_len);
        n_offset += n_owner_len;

        // Read target length
        uint32_t n_target_len_network;
        std::memcpy(&n_target_len_network, str_data.data() + n_offset, BLOCK_UINT32_SIZE);
        uint32_t n_target_len = ntohl(n_target_len_network);
        n_offset += BLOCK_UINT32_SIZE;

        // Read target
        if (n_offset + n_target_len > str_data.length()) return nullptr;
        std::string str_target(str_data.data() + n_offset, n_target_len);
        n_offset += n_target_len;

        // Read data length
        uint32_t n_data_len_network;
        std::memcpy(&n_data_len_network, str_data.data() + n_offset, BLOCK_UINT32_SIZE);
        uint32_t n_data_len = ntohl(n_data_len_network);
        n_offset += BLOCK_UINT32_SIZE;

        // Read data
        if (n_offset + n_data_len > str_data.length()) return nullptr;
        std::vector<uint8_t> data(str_data.data() + n_offset, str_data.data() + n_offset + n_data_len);
        n_offset += n_data_len;

        // Read reward
        uint64_t n_reward_network;
        std::memcpy(&n_reward_network, str_data.data() + n_offset, BLOCK_UINT64_SIZE);
        uint64_t n_reward = be64toh(n_reward_network);
        n_offset += BLOCK_UINT64_SIZE;

        // Read timestamp
        uint64_t n_timestamp_network;
        std::memcpy(&n_timestamp_network, str_data.data() + n_offset, BLOCK_INT64_SIZE);
        int64_t n_timestamp = static_cast<int64_t>(be64toh(n_timestamp_network));
        n_offset += BLOCK_INT64_SIZE;

        // Read type
        uint8_t n_type;
        std::memcpy(&n_type, str_data.data() + n_offset, BLOCK_UINT8_SIZE);
        TransactionType type = static_cast<TransactionType>(n_type);
        n_offset += BLOCK_UINT8_SIZE;

        // Read metadata length
        uint32_t n_metadata_len_network;
        std::memcpy(&n_metadata_len_network, str_data.data() + n_offset, BLOCK_UINT32_SIZE);
        uint32_t n_metadata_len = ntohl(n_metadata_len_network);
        n_offset += BLOCK_UINT32_SIZE;

        // Read metadata
        if (n_offset + n_metadata_len > str_data.length()) return nullptr;
        std::string str_metadata(str_data.data() + n_offset, n_metadata_len);

        // Create transaction object (note: constructor will regenerate ID and timestamp)
        auto p_tx = std::make_shared<CTransaction>(str_owner, str_target, data, n_reward, type, str_metadata);

        // Override timestamp to match serialized value
        p_tx->m_n_timestamp = n_timestamp;

        // Recompute ID with correct timestamp
        std::string str_id_input = str_owner + str_target;
        str_id_input.append(reinterpret_cast<const char*>(data.data()), data.size());
        str_id_input += std::to_string(n_reward);
        str_id_input += std::to_string(n_timestamp);
        str_id_input += std::to_string(static_cast<uint8_t>(type));
        str_id_input += str_metadata;
        p_tx->m_id = CHash::ComputeSHA256(str_id_input);

        return p_tx;
    } catch (...) {
        return nullptr;  // Deserialization error
    }
}

/**
 * @brief Convert transaction to JSON string format
 * @return JSON string containing transaction information
 *
 * Returns JSON object with all transaction fields.
 * Binary data is returned as hex string for JSON compatibility.
 */
std::string CTransaction::ToJson() const {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"transaction_id\": \"" << m_id.GetData() << "\",\n";
    oss << "  \"owner\": \"" << EscapeJsonString(m_str_owner) << "\",\n";
    oss << "  \"target\": \"" << EscapeJsonString(m_str_target) << "\",\n";
    oss << "  \"data_hex\": \"" << BytesToHex(m_data) << "\",\n";
    oss << "  \"data_size\": " << m_n_data_size << ",\n";
    oss << "  \"reward\": " << m_n_reward << ",\n";
    oss << "  \"timestamp\": " << m_n_timestamp << ",\n";
    oss << "  \"type\": \"" << TransactionTypeToString(m_type) << "\",\n";
    oss << "  \"metadata\": \"" << EscapeJsonString(m_str_metadata) << "\"\n";
    oss << "}";

    return oss.str();
}
