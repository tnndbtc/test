// ============= transaction_payload.h =============
#ifndef TRANSACTION_PAYLOAD_H
#define TRANSACTION_PAYLOAD_H

#include <vector>
#include <cstdint>
#include <memory>
#include <string>

/**
 * @file transaction_payload.h
 * @brief Transaction payload structures for different transaction types
 *
 * Defines payload structures for each TransactionType. The binary data
 * in CTransaction::m_data field is interpreted based on the transaction
 * type and serialized/deserialized using these structures.
 */

/**
 * @struct CTransferPayload
 * @brief Payload for TRANSFER transaction type
 *
 * Contains destination address and amount for simple value transfers.
 * Binary serialization format:
 *   - to: 20 bytes (destination address)
 *   - amount: 8 bytes (uint64_t, little-endian)
 */
struct CTransferPayload {
    std::vector<uint8_t> m_to;      ///< Destination address (20 bytes)
    uint64_t m_n_amount;            ///< Transfer amount in smallest currency unit

    /**
     * @brief Construct transfer payload
     * @param to Destination address (must be 20 bytes)
     * @param n_amount Transfer amount
     */
    CTransferPayload(const std::vector<uint8_t>& to, uint64_t n_amount);

    /**
     * @brief Default constructor for deserialization
     */
    CTransferPayload();

    /**
     * @brief Serialize payload to binary format
     * @return Binary data (28 bytes: 20 bytes address + 8 bytes amount)
     */
    std::vector<uint8_t> Serialize() const;

    /**
     * @brief Deserialize payload from binary format
     * @param data Binary data to deserialize
     * @return Shared pointer to payload, or nullptr on error
     *
     * Validates:
     * - Data length is exactly 28 bytes
     * - Address is 20 bytes
     */
    static std::shared_ptr<CTransferPayload> Deserialize(const std::vector<uint8_t>& data);

    /**
     * @brief Convert payload to JSON object content
     * @return JSON object content (without outer braces)
     *
     * Example output: "to": "0xABCD...", "amount": 500000
     */
    std::string ToJson() const;
};

#endif // TRANSACTION_PAYLOAD_H
