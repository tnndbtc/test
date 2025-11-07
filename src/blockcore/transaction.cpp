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
#include <chrono>
#include <cstring>

// Platform-specific byte order conversion
#ifdef __APPLE__
    #include <machine/endian.h>
    #include <libkern/OSByteOrder.h>
    #define htobe64(x) OSSwapHostToBigInt64(x)
    #define be64toh(x) OSSwapBigToHostInt64(x)
#elif defined(__linux__)
    #include <endian.h>
#elif defined(_WIN32)
    #include <winsock2.h>
    #define htobe64(x) htonll(x)
    #define be64toh(x) ntohll(x)
#endif

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
    : m_str_owner(str_owner), m_str_target(str_target), m_data(data),
      m_n_data_size(data.size()), m_n_reward(n_reward),
      m_type(TransactionType::TRANSFER), m_str_metadata("") {
    m_n_timestamp = std::chrono::system_clock::now().time_since_epoch().count();

    // Compute transaction ID from all fields to ensure data integrity
    std::string str_id_input = str_owner + str_target;
    str_id_input.append(reinterpret_cast<const char*>(data.data()), data.size());
    str_id_input += std::to_string(n_reward);
    str_id_input += std::to_string(m_n_timestamp);
    str_id_input += std::to_string(static_cast<uint8_t>(m_type));

    m_id = CHash(str_id_input);
}

/**
 * @brief Construct transaction with type and metadata
 * @param str_owner Address of transaction creator/sender
 * @param str_target Address of transaction recipient
 * @param data Binary data payload to store
 * @param n_reward Mining reward/fee for including this transaction
 * @param type Transaction type (TRANSFER, STORAGE, COMPUTE)
 * @param str_meta Service-specific metadata in JSON format
 *
 * Initializes transaction with specified type and metadata:
 * - For STORAGE: metadata should contain file_hash, storage_duration, filename
 * - For COMPUTE: metadata should contain task_desc, cpu_cores, memory_gb, duration_hours
 * - For TRANSFER: metadata is typically empty
 */
CTransaction::CTransaction(const std::string& str_owner, const std::string& str_target,
                           const std::vector<uint8_t>& data, uint64_t n_reward,
                           TransactionType type, const std::string& str_meta)
    : m_str_owner(str_owner), m_str_target(str_target), m_data(data),
      m_n_data_size(data.size()), m_n_reward(n_reward),
      m_type(type), m_str_metadata(str_meta) {
    m_n_timestamp = std::chrono::system_clock::now().time_since_epoch().count();

    // Compute transaction ID from all fields including metadata
    std::string str_id_input = str_owner + str_target;
    str_id_input.append(reinterpret_cast<const char*>(data.data()), data.size());
    str_id_input += std::to_string(n_reward);
    str_id_input += std::to_string(m_n_timestamp);
    str_id_input += std::to_string(static_cast<uint8_t>(m_type));
    str_id_input += str_meta;

    m_id = CHash(str_id_input);
}

std::string CTransaction::Serialize() const {
    std::string str_result;

    // Write owner (length + data)
    uint32_t n_owner_len = htonl(static_cast<uint32_t>(m_str_owner.length()));
    str_result.append(reinterpret_cast<const char*>(&n_owner_len), BLOCK_UINT32_SIZE);
    str_result.append(m_str_owner);

    // Write target (length + data)
    uint32_t n_target_len = htonl(static_cast<uint32_t>(m_str_target.length()));
    str_result.append(reinterpret_cast<const char*>(&n_target_len), BLOCK_UINT32_SIZE);
    str_result.append(m_str_target);

    // Write data (length + data)
    uint32_t n_data_len = htonl(static_cast<uint32_t>(m_data.size()));
    str_result.append(reinterpret_cast<const char*>(&n_data_len), BLOCK_UINT32_SIZE);
    str_result.append(reinterpret_cast<const char*>(m_data.data()), m_data.size());

    // Write reward (BLOCK_UINT64_SIZE bytes, network byte order)
    uint64_t n_reward_network = htobe64(m_n_reward);
    str_result.append(reinterpret_cast<const char*>(&n_reward_network), BLOCK_UINT64_SIZE);

    // Write timestamp (BLOCK_INT64_SIZE bytes, network byte order)
    uint64_t n_timestamp_network = htobe64(static_cast<uint64_t>(m_n_timestamp));
    str_result.append(reinterpret_cast<const char*>(&n_timestamp_network), BLOCK_INT64_SIZE);

    // Write type (BLOCK_UINT8_SIZE byte)
    uint8_t n_type = static_cast<uint8_t>(m_type);
    str_result.append(reinterpret_cast<const char*>(&n_type), BLOCK_UINT8_SIZE);

    // Write metadata (length + data)
    uint32_t n_metadata_len = htonl(static_cast<uint32_t>(m_str_metadata.length()));
    str_result.append(reinterpret_cast<const char*>(&n_metadata_len), BLOCK_UINT32_SIZE);
    str_result.append(m_str_metadata);

    return str_result;
}

std::shared_ptr<CTransaction> CTransaction::Deserialize(const std::string& str_data) {
    size_t n_offset = 0;

    // Minimum size check
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
        p_tx->m_id = CHash(str_id_input);

        return p_tx;
    } catch (...) {
        return nullptr;  // Deserialization error
    }
}
