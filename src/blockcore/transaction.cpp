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
 *   owner + target + timestamp
 *
 * The transaction ID ensures uniqueness even for identical data
 * by including the timestamp in the hash computation.
 */
CTransaction::CTransaction(const std::string& str_owner, const std::string& str_target,
                           const std::vector<uint8_t>& data, uint64_t n_reward)
    : m_str_owner(str_owner), m_str_target(str_target), m_data(data),
      m_n_data_size(data.size()), m_n_reward(n_reward) {
    m_n_timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    m_id = CHash(str_owner + str_target + std::to_string(m_n_timestamp));
}
