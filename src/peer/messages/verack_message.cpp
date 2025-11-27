// ============= verack_message.cpp =============
#include "verack_message.h"
#include "logger/logger.h"

/**
 * @brief Constructor with magic
 */
CVerackMessage::CVerackMessage(uint32_t n_magic)
    : CPeerMessage(n_magic) {
}

/**
 * @brief Get message type
 */
std::string CVerackMessage::GetType() const {
    return MessageType::VERACK;
}

/**
 * @brief Serialize (empty payload)
 */
std::vector<uint8_t> CVerackMessage::SerializePayload() const {
    LOG_TRACE("CVerackMessage::SerializePayload - Starting serialization");
    LOG_TRACE("CVerackMessage::SerializePayload - Success: 0 bytes (empty payload)");
    return std::vector<uint8_t>();  // Empty payload
}

/**
 * @brief Deserialize (empty payload)
 */
bool CVerackMessage::DeserializePayload(const std::vector<uint8_t>& payload) {
    // Accept any payload (including empty)
    // Suppress unused warning
    (void)payload;
    return true;
}

/**
 * @brief Create a copy of this message
 */
std::unique_ptr<CPeerMessage> CVerackMessage::Clone() const {
    return std::make_unique<CVerackMessage>(GetMagic());
}
