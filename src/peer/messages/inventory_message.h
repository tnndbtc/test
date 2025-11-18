// ============= inventory_message.h =============
#ifndef INVENTORY_MESSAGE_H
#define INVENTORY_MESSAGE_H

#include "peer/peer_message.h"
#include "blockcore/object_type.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <string>

/**
 * @struct CInventoryItem
 * @brief Represents a single inventory item (object type + hash)
 *
 * Used in INVENTORY and GETDATA messages to identify transactions or blocks.
 */
struct CInventoryItem {
    ObjectType::Type type;  ///< Object type (TX, BLOCK, etc.)
    std::string str_hash;   ///< Binary hash (32 bytes)

    /**
     * @brief Constructor
     */
    CInventoryItem(ObjectType::Type t, const std::string& hash)
        : type(t), str_hash(hash) {}

    /**
     * @brief Default constructor
     */
    CInventoryItem() : type(ObjectType::OBJ_BEGIN), str_hash() {}
};

/**
 * @class CInventoryMessage
 * @brief Type-safe INVENTORY message implementation
 *
 * INVENTORY messages broadcast hashes of new transactions or blocks to peers.
 * Peers can then request full objects using GETDATA if they don't have them.
 *
 * Payload format: [count:4bytes][type:2bytes][hash:32bytes]...
 * - Count: uint32_t in network byte order
 * - Type: ObjectType::Type (uint16_t) in network byte order
 * - Hash: 32 bytes binary SHA-256 hash
 *
 * Usage:
 *   CInventoryMessage inv(LOCALNET_MAGIC);
 *   inv.AddItem(ObjectType::OBJ_TX, tx_hash);
 *   inv.AddItem(ObjectType::OBJ_BLOCK, block_hash);
 *   std::string serialized = inv.Serialize();
 */
class CInventoryMessage : public CPeerMessage {
private:
    std::vector<CInventoryItem> m_vec_items;  ///< List of inventory items

public:
    /**
     * @brief Constructor with magic
     */
    explicit CInventoryMessage(uint32_t n_magic);

    /**
     * @brief Constructor with items and magic
     */
    CInventoryMessage(const std::vector<CInventoryItem>& vec_items, uint32_t n_magic);

    /**
     * @brief Get message type (always returns "inv")
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
     * @brief Get all inventory items
     */
    const std::vector<CInventoryItem>& GetItems() const;

    /**
     * @brief Add an inventory item
     */
    void AddItem(ObjectType::Type type, const std::string& str_hash);

    /**
     * @brief Get number of items
     */
    size_t GetItemCount() const;
};

#endif // INVENTORY_MESSAGE_H
