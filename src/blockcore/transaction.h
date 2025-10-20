// ============= transaction.h =============
#ifndef TRANSACTION_H
#define TRANSACTION_H

#include "utils/hash.h"
#include <string>
#include <vector>
#include <cstdint>

/**
 * @struct CTransaction
 * @brief Represents a data storage transaction in the blockweave
 *
 * Transactions are the fundamental units of data storage in the blockweave.
 * Each transaction contains arbitrary binary data along with metadata about
 * ownership, target, and rewards. Transaction IDs are computed as SHA-256
 * hashes of the transaction contents.
 *
 * Key features:
 * - Stores arbitrary binary data (files, messages, etc.)
 * - Owner and target addresses for tracking data flow
 * - Reward/fee system for miners
 * - Unique cryptographic ID based on content
 * - Timestamp for chronological ordering
 *
 * Example usage:
 *   std::vector<uint8_t> data = {'H', 'e', 'l', 'l', 'o'};
 *   CTransaction tx("owner_address", "target_address", data, 1000);
 *   // tx.m_id now contains SHA-256 hash of transaction
 */
struct CTransaction {
    CHash m_id;                    ///< Unique transaction ID (SHA-256 hash of tx data)
    std::string m_str_owner;       ///< Address of transaction creator/sender
    std::string m_str_target;      ///< Address of transaction recipient
    std::vector<uint8_t> m_data;   ///< Binary data payload (file contents, message, etc.)
    size_t m_n_data_size;          ///< Size of data in bytes (cached for performance)
    uint64_t m_n_reward;           ///< Mining reward/fee in smallest currency unit
    int64_t m_n_timestamp;         ///< Unix timestamp (nanoseconds) when transaction created

    /**
     * @brief Construct a new transaction
     * @param str_owner Owner/sender address
     * @param str_target Target/recipient address
     * @param data Binary data payload
     * @param n_reward Mining reward/fee
     *
     * Automatically generates:
     * - Transaction ID from hash of all transaction data
     * - Timestamp from system clock
     * - Data size from data vector
     */
    CTransaction(const std::string& str_owner, const std::string& str_target,
                 const std::vector<uint8_t>& data, uint64_t n_reward);
};

#endif
