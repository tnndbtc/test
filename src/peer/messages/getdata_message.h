// ============= getdata_message.h =============
#ifndef GETDATA_MESSAGE_H
#define GETDATA_MESSAGE_H

#include "peer/peer_message.h"
#include "inventory_message.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <string>

/**
 * @class CGetDataMessage
 * @brief Type-safe GETDATA message implementation
 *
 * GETDATA messages request full objects (transactions or blocks) from peers.
 * Sent in response to INVENTORY messages when we don't have the objects.
 *
 * Payload format is identical to INVENTORY:
 * [count:4bytes][type:2bytes][hash:32bytes]...
 *
 * Usage:
 *   CGetDataMessage getdata(LOCALNET_MAGIC);
 *   getdata.AddItem(ObjectType::OBJ_TX, tx_hash);
 *   getdata.AddItem(ObjectType::OBJ_BLOCK, block_hash);
 *   std::string serialized = getdata.Serialize();
 */
class CGetDataMessage : public CPeerMessage {
private:
    std::vector<CInventoryItem> m_vec_items;  ///< List of requested items

public:
    /**
     * @brief Constructor with magic
     */
    explicit CGetDataMessage(uint32_t n_magic);

    /**
     * @brief Constructor with items and magic
     */
    CGetDataMessage(const std::vector<CInventoryItem>& vec_items, uint32_t n_magic);

    /**
     * @brief Get message type (always returns "getdata")
     */
    std::string GetType() const override;

    /**
     * @brief Serialize items to binary payload
     */
    std::vector<uint8_t> SerializePayload() const override;

    /**
     * @brief Deserialize binary payload to items
     */
    bool DeserializePayload(const std::vector<uint8_t>& payload) override;

    /**
     * @brief Create a copy of this message
     */
    std::unique_ptr<CPeerMessage> Clone() const override;

    /**
     * @brief Validate message (checks item count and hash sizes)
     */

    /**
     * @brief Get all requested items
     */
    const std::vector<CInventoryItem>& GetItems() const;

    /**
     * @brief Add a requested item
     */
    void AddItem(ObjectType::Type type, const std::string& str_hash);

    /**
     * @brief Get number of items
     */
    size_t GetItemCount() const;
};

#endif // GETDATA_MESSAGE_H
