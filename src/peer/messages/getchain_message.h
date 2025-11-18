// ============= getchain_message.h =============
#ifndef GETCHAIN_MESSAGE_H
#define GETCHAIN_MESSAGE_H

#include "peer/peer_message.h"
#include <cstdint>
#include <memory>

/**
 * @class CGetChainMessage
 * @brief Type-safe GET_CHAIN message implementation
 *
 * GET_CHAIN messages request blockchain information from a peer.
 * This is a simple request message with no payload.
 *
 * Payload format: empty
 *
 * Usage:
 *   CGetChainMessage getchain(LOCALNET_MAGIC);
 *   std::string serialized = getchain.Serialize();
 */
class CGetChainMessage : public CPeerMessage {
public:
    /**
     * @brief Constructor with magic
     */
    explicit CGetChainMessage(uint32_t n_magic);

    /**
     * @brief Get message type (always returns "get_chain")
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

#endif // GETCHAIN_MESSAGE_H
