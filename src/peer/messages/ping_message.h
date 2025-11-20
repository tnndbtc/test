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
 * The nonce is automatically generated in the constructor.
 *
 * Payload format: 4 bytes (uint32_t nonce in big-endian)
 *
 * Usage:
 *   auto ping = std::make_unique<CPingMessage>(LOCALNET_MAGIC);
 *   uint32_t nonce = ping->GetNonce();  // Get auto-generated nonce
 *   std::string serialized = ping->Serialize();
 *   send_to_peer(serialized);
 */
class CPingMessage : public CPeerMessage {
private:
    uint64_t m_n_nonce;  ///< Random nonce for matching with PONG response

public:
    /**
     * @brief Constructor with magic - auto-generates random nonce
     * @param n_magic Network magic bytes
     */
    CPingMessage(uint32_t n_magic);

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
};

#endif // PING_MESSAGE_H
