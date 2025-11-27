// ============= getpeers_message.cpp =============
#include "getpeers_message.h"
#include "logger/logger.h"

/**
 * @brief Constructor with magic
 */
CGetPeersMessage::CGetPeersMessage(uint32_t n_magic)
    : CPeerMessage(n_magic) {
}

/**
 * @brief Get message type
 */
std::string CGetPeersMessage::GetType() const {
    return MessageType::GET_PEERS;
}

/**
 * @brief Serialize (empty payload)
 */
std::vector<uint8_t> CGetPeersMessage::SerializePayload() const {
    LOG_TRACE("CGetPeersMessage::SerializePayload - Starting serialization");
    LOG_TRACE("CGetPeersMessage::SerializePayload - Success: 0 bytes (empty payload)");
    return std::vector<uint8_t>();  // Empty payload
}

/**
 * @brief Deserialize (empty payload)
 */
bool CGetPeersMessage::DeserializePayload(const std::vector<uint8_t>& payload) {
    // Accept any payload (including empty)
    // Suppress unused warning
    (void)payload;
    return true;
}

/**
 * @brief Create a copy of this message
 */
std::unique_ptr<CPeerMessage> CGetPeersMessage::Clone() const {
    return std::make_unique<CGetPeersMessage>(GetMagic());
}

