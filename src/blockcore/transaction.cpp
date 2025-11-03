// ============= transaction.cpp =============
/**
 * @file transaction.cpp
 * @brief Implementation of blockweave transaction
 *
 * Provides transaction construction with automatic ID generation
 * and timestamping for data storage operations.
 */

#include "transaction.h"
#include <chrono>

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
