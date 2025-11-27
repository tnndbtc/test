// ============= inventory_message.cpp =============
#include "inventory_message.h"
#include "utils/settings.h"
#include "logger/logger.h"
#include <cstring>

// Include network byte order functions
#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <arpa/inet.h>
#endif

/**
 * @brief Constructor with magic
 */
CInventoryMessage::CInventoryMessage(uint32_t n_magic)
    : CPeerMessage(n_magic), m_vec_items() {
}

/**
 * @brief Constructor with items and magic
 */
CInventoryMessage::CInventoryMessage(const std::vector<CInventoryItem>& vec_items, uint32_t n_magic)
    : CPeerMessage(n_magic), m_vec_items(vec_items) {
}

/**
 * @brief Get message type
 */
std::string CInventoryMessage::GetType() const {
    return MessageType::INVENTORY;
}

/**
 * @brief Serialize items to binary payload
 * Format: [count:4bytes][type:2bytes][hash:32bytes]...
 */
std::vector<uint8_t> CInventoryMessage::SerializePayload() const {
    LOG_TRACE("CInventoryMessage::SerializePayload - Starting serialization");
    // Calculate size: 4 bytes count + n * (2 bytes type + 32 bytes hash)
    size_t n_payload_size = MESSAGE_COUNT_SIZE + (m_vec_items.size() * (MESSAGE_TYPE_SIZE + MESSAGE_HASH_SIZE));
    std::vector<uint8_t> payload(n_payload_size);

    size_t n_offset = 0;

    // Write count (4 bytes, network byte order)
    uint32_t n_count = static_cast<uint32_t>(m_vec_items.size());
    uint32_t n_count_network = htonl(n_count);
    std::memcpy(payload.data() + n_offset, &n_count_network, MESSAGE_COUNT_SIZE);
    n_offset += MESSAGE_COUNT_SIZE;

    // Write each item
    for (const auto& item : m_vec_items) {
    LOG_TRACE("CInventoryMessage::SerializePayload - Starting serialization");
        // Write type (2 bytes, network byte order)
        uint16_t n_type_network = htons(static_cast<uint16_t>(item.type));
        std::memcpy(payload.data() + n_offset, &n_type_network, MESSAGE_TYPE_SIZE);
        n_offset += MESSAGE_TYPE_SIZE;

        // Write hash (32 bytes, binary)
        std::memcpy(payload.data() + n_offset, item.str_hash.data(), MESSAGE_HASH_SIZE);
        n_offset += MESSAGE_HASH_SIZE;
    }

    LOG_TRACE("CInventoryMessage::SerializePayload - Success: " + std::to_string(payload.size()) + " bytes");
    return payload;
}

/**
 * @brief Deserialize binary payload to items
 */
bool CInventoryMessage::DeserializePayload(const std::vector<uint8_t>& payload) {
    m_vec_items.clear();

    // Need at least 4 bytes for count
    if (payload.size() < MESSAGE_COUNT_SIZE) {
        LOG_WARN("payload length is smaller than: " + std::to_string(MESSAGE_COUNT_SIZE));
        return false;
    }

    size_t n_offset = 0;

    // Read count (4 bytes, network byte order)
    uint32_t n_count_network;
    std::memcpy(&n_count_network, payload.data() + n_offset, MESSAGE_COUNT_SIZE);
    uint32_t n_count = ntohl(n_count_network);
    n_offset += MESSAGE_COUNT_SIZE;

    // Validate payload size
    size_t n_expected_size = MESSAGE_COUNT_SIZE + (n_count * (MESSAGE_TYPE_SIZE + MESSAGE_HASH_SIZE));
    if (payload.size() < n_expected_size) {
        LOG_WARN("No enough payload data: received: " + std::to_string(payload.size()) + " expected: " + std::to_string(n_expected_size));
        return false;  // Payload too short
    }

    // Read each item
    for (uint32_t i = 0; i < n_count; i++) {
        // Read type (2 bytes, network byte order)
        uint16_t n_type_network;
        std::memcpy(&n_type_network, payload.data() + n_offset, MESSAGE_TYPE_SIZE);
        ObjectType::Type obj_type = static_cast<ObjectType::Type>(ntohs(n_type_network));
        n_offset += MESSAGE_TYPE_SIZE;

        // Validation: Check object type is in valid range
        if (obj_type <= ObjectType::OBJ_BEGIN || obj_type >= ObjectType::OBJ_LAST) {
            LOG_WARN("object type is invalid. Received: " + std::to_string(obj_type));
            return false;
        }

        // Read hash (32 bytes, binary)
        std::string str_hash(reinterpret_cast<const char*>(payload.data() + n_offset), MESSAGE_HASH_SIZE);
        n_offset += MESSAGE_HASH_SIZE;

        // Validation: Check hash size is correct (should always be 32 from the read above, but verify)
        if (str_hash.size() != MESSAGE_HASH_SIZE) {
            LOG_WARN("hash size is incorrect.  Received: " + std::to_string(str_hash.size()) + " Expected: " + std::to_string(MESSAGE_HASH_SIZE));
            return false;
        }

        // Add item
        m_vec_items.emplace_back(obj_type, str_hash);
    }

    return true;
}

/**
 * @brief Create a copy of this message
 */
std::unique_ptr<CPeerMessage> CInventoryMessage::Clone() const {
    return std::make_unique<CInventoryMessage>(m_vec_items, GetMagic());
}

/**
 * @brief Get all inventory items
 */
const std::vector<CInventoryItem>& CInventoryMessage::GetItems() const {
    return m_vec_items;
}

/**
 * @brief Add an inventory item
 */
void CInventoryMessage::AddItem(ObjectType::Type type, const std::string& str_hash) {
    m_vec_items.emplace_back(type, str_hash);
}

/**
 * @brief Get number of items
 */
size_t CInventoryMessage::GetItemCount() const {
    return m_vec_items.size();
}
