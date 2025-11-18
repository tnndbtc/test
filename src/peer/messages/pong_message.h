// ============= pong_message.h =============
#ifndef PONG_MESSAGE_H
#define PONG_MESSAGE_H

#include "peer/peer_message.h"
#include <cstdint>
#include <memory>

/**
 * @class CPongMessage
 * @brief Type-safe PONG message implementation
 *
 * PONG messages are sent in response to PING messages, echoing back the
 * same nonce value to verify the connection is alive.
 *
 * Payload format: 4 bytes (uint32_t nonce in big-endian)
 *
 * Usage:
 *   auto pong = std::make_unique<CPongMessage>(ping_nonce, LOCALNET_MAGIC);
 *   std::string serialized = pong->Serialize();
 *   send_to_peer(serialized);
 */
class CPongMessage : public CPeerMessage {
private:
    uint32_t m_n_nonce;  ///< Nonce echoed from PING message

public:
    /**
     * @brief Constructor with nonce and magic
     * @param n_nonce Nonce value from PING message
     * @param n_magic Network magic bytes
     */
    CPongMessage(uint32_t n_nonce, uint32_t n_magic);

    /**
     * @brief Get message type (always returns "pong")
     */
    std::string GetType() const override;

    /**
     * @brief Serialize nonce to 4-byte big-endian payload
     */
    std::vector<uint8_t> SerializePayload() const override;

    /**
     * @brief Deserialize 4-byte big-endian payload to nonce
     * @return true if payload is exactly 4 bytes, false otherwise
     */
    bool DeserializePayload(const std::vector<uint8_t>& payload) override;

    /**
     * @brief Create a copy of this message
     */
    std::unique_ptr<CPeerMessage> Clone() const override;

    /**
     * @brief Validate message (always true for PONG)
     */

    /**
     * @brief Get nonce value
     */
    uint32_t GetNonce() const;

    /**
     * @brief Set nonce value
     */
    void SetNonce(uint32_t n_nonce);
};

#endif // PONG_MESSAGE_H
