// ============= getpeers_message.h =============
#ifndef GETPEERS_MESSAGE_H
#define GETPEERS_MESSAGE_H

#include "peer/peer_message.h"
#include <cstdint>
#include <memory>

/**
 * @class CGetPeersMessage
 * @brief Type-safe GET_PEERS message implementation
 *
 * GET_PEERS messages request a list of known peers from a peer.
 * This is a simple request message with no payload.
 *
 * Payload format: empty
 *
 * Usage:
 *   CGetPeersMessage getpeers(LOCALNET_MAGIC);
 *   std::string serialized = getpeers.Serialize();
 */
class CGetPeersMessage : public CPeerMessage {
public:
    /**
     * @brief Constructor with magic
     */
    explicit CGetPeersMessage(uint32_t n_magic);

    /**
     * @brief Get message type (always returns "get_peers")
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

    /**
     * @brief Validate message (always true)
     */
};

#endif // GETPEERS_MESSAGE_H
