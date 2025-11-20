// ============= verack_message.h =============
#ifndef VERACK_MESSAGE_H
#define VERACK_MESSAGE_H

#include "peer/peer_message.h"
#include <cstdint>
#include <memory>

/**
 * @class CVerackMessage
 * @brief Type-safe VERACK message implementation
 *
 * VERACK messages are sent in response to VERSION messages during the
 * connection handshake protocol. This acknowledges receipt of the VERSION
 * message and completes the handshake sequence.
 *
 * Payload format: empty
 *
 * Usage:
 *   CVerackMessage verack(LOCALNET_MAGIC);
 *   std::string serialized = verack.Serialize();
 */
class CVerackMessage : public CPeerMessage {
public:
    /**
     * @brief Constructor with magic
     */
    explicit CVerackMessage(uint32_t n_magic);

    /**
     * @brief Get message type (always returns "verack")
     */
    std::string GetType() const override;

    /**
     * @brief Serialize (empty payload)
     */
    std::vector<uint8_t> SerializePayload() const override;

    /**
     * @brief Deserialize (empty payload)
     */
    bool DeserializePayload(const std::vector<uint8_t>& payload) override;

    /**
     * @brief Create a copy of this message
     */
    std::unique_ptr<CPeerMessage> Clone() const override;
};

#endif // VERACK_MESSAGE_H
