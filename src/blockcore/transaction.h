// ============= transaction.h =============
#ifndef TRANSACTION_H
#define TRANSACTION_H

#include "utils/hash.h"
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

/**
 * @enum TxVersion
 * @brief Transaction version for protocol versioning
 */
enum class TxVersion : uint8_t {
    V0 = 0       ///< Version 0 (initial version)
};

/**
 * @enum TransactionType
 * @brief Types of transactions supported by the blockweave
 */
enum class TransactionType : uint8_t {
    TRANSFER = 0       ///< Basic coin/token transfer
};

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

    // Core transaction fields
    TxVersion m_n_version;              ///< Protocol version
    uint64_t m_n_nonce;                 ///< Unique nonce to prevent replay attacks
    std::vector<uint8_t> m_from;        ///< Sender address (20 bytes)
    TransactionType m_type;             ///< Type of transaction (uint8_t enum)
    std::vector<uint8_t> m_data;        ///< Typed payload (format depends on m_type)
    uint64_t m_n_fee;                   ///< Transaction fee in smallest currency unit
    std::vector<uint8_t> m_signature;   ///< Cryptographic signature over transaction fields

    // Legacy fields (maintained for backward compatibility)
    size_t m_n_data_size;          ///< Size of data in bytes (cached for performance)

    // Service-specific metadata (stored as JSON string for flexibility)
    std::string m_str_metadata;    ///< Service-specific data in JSON format

    /**
     * @brief Default constructor for CTransaction
     *
     * Creates an empty transaction. Transactions should be populated
     * via deserialization using the Deserialize() static method.
     */
    CTransaction() = default;

    /**
     * @brief Serialize transaction to binary format for network transmission
     * @return Binary string containing serialized transaction data
     *
     * Format: [version:1][nonce:8][from:20][type:1][data_len:4][data]
     *         [fee:8][sig_len:4][signature][metadata_len:4][metadata]
     */
    std::string Serialize() const;

    /**
     * @brief Deserialize transaction from binary format
     * @param str_data Binary string containing serialized transaction
     * @return Shared pointer to deserialized transaction, or nullptr on error
     */
    static std::shared_ptr<CTransaction> Deserialize(const std::string& str_data);

    /**
     * @brief Convert transaction to JSON string format
     * @return JSON string containing transaction information
     *
     * Returns JSON object with all transaction fields.
     * Binary data is returned as hex string.
     * Used by REST API handlers and logging.
     */
    std::string ToJson() const;
};

#endif
