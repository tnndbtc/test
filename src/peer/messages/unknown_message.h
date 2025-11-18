// ============= unknown_message.h =============
#ifndef UNKNOWN_MESSAGE_H
#define UNKNOWN_MESSAGE_H

#include "peer/peer_message.h"
#include <cstdint>
#include <memory>

/**
 * @class CUnknownMessage
 * @brief Type-safe UNKNOWN message implementation
 *
 * UNKNOWN messages represent unrecognized or invalid message types.
 * This is used as a placeholder when receiving unknown message types from peers.
 *
 * Payload format: Empty (0 bytes)
 *
 * Usage:
 *   auto unknown = std::make_unique<CUnknownMessage>(LOCALNET_MAGIC);
 */
class CUnknownMessage : public CPeerMessage {
public:
    /**
     * @brief Constructor with magic
     * @param n_magic Network magic bytes
     */
    explicit CUnknownMessage(uint32_t n_magic);

    /**
     * @brief Get message type (always returns "unknown")
     */
    std::string GetType() const override;

    /**
     * @brief Serialize empty payload
     */
    std::vector<uint8_t> SerializePayload() const override;

    /**
     * @brief Deserialize payload (accepts any payload)
     * @return true always
     */
    bool DeserializePayload(const std::vector<uint8_t>& payload) override;

    /**
     * @brief Create a copy of this message
     */
    std::unique_ptr<CPeerMessage> Clone() const override;

    /**
     * @brief Validate message (always false for UNKNOWN)
     */

    /**
     * @brief Get payload size (always returns 0)
     */
    size_t GetPayloadSize() const;
};

#endif // UNKNOWN_MESSAGE_H
