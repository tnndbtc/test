// ============= block_message.h =============
#ifndef BLOCK_MESSAGE_H
#define BLOCK_MESSAGE_H

#include "peer/peer_message.h"
#include "blockcore/block.h"
#include <cstdint>
#include <memory>
#include <string>

/**
 * @class CBlockMessage
 * @brief Type-safe BLOCK message implementation
 *
 * BLOCK messages transmit serialized block data between peers.
 * Sent in response to GETDATA requests for block objects.
 *
 * Payload format: [N bytes serialized_block]
 *
 * Usage:
 *   auto p_block = std::make_shared<CBlock>(...);
 *   CBlockMessage block_msg(p_block, LOCALNET_MAGIC);
 *   std::string serialized = block_msg.Serialize();
 */
class CBlockMessage : public CPeerMessage {
private:
    std::shared_ptr<CBlock> m_p_block;  ///< Block object

public:
    /**
     * @brief Constructor with block and magic
     */
    CBlockMessage(std::shared_ptr<CBlock> p_block, uint32_t n_magic);

    /**
     * @brief Get message type (always returns "block")
     */
    std::string GetType() const override;

    /**
     * @brief Serialize block data to binary payload
     */
    std::vector<uint8_t> SerializePayload() const override;

    /**
     * @brief Deserialize binary payload to block data
     */
    bool DeserializePayload(const std::vector<uint8_t>& payload) override;

    /**
     * @brief Create a copy of this message
     */
    std::unique_ptr<CPeerMessage> Clone() const override;

    /**
     * @brief Validate message (checks data not empty)
     */

    /**
     * @brief Get block object
     */
    std::shared_ptr<CBlock> GetBlock() const;
};

#endif // BLOCK_MESSAGE_H
