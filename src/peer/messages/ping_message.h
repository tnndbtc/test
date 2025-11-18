// ============= ping_message.h =============
#ifndef PING_MESSAGE_H
#define PING_MESSAGE_H

#include "peer/peer_message.h"
#include <cstdint>
#include <memory>

/**
 * @class CPingMessage
 * @brief Type-safe PING message implementation
 *
 * PING messages are sent periodically (every 30 seconds) to maintain connections
 * and verify peer liveness. The nonce allows matching PING with PONG responses.
 *
 * Payload format: 4 bytes (uint32_t nonce in big-endian)
 *
 * Usage:
 *   auto ping = std::make_unique<CPingMessage>(12345, LOCALNET_MAGIC);
 *   std::string serialized = ping->Serialize();
 *   send_to_peer(serialized);
 */
class CPingMessage : public CPeerMessage {
private:
    uint32_t m_n_nonce;  ///< Random nonce for matching with PONG response

public:
    /**
     * @brief Constructor with nonce and magic
     * @param n_nonce Random nonce value
     * @param n_magic Network magic bytes
     */
    CPingMessage(uint32_t n_nonce, uint32_t n_magic);

    /**
     * @brief Get message type (always returns "ping")
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
     * @brief Validate message (always true for PING)
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

#endif // PING_MESSAGE_H
