// ============= unknown_message.cpp =============
#include "unknown_message.h"
#include "logger/logger.h"

/**
 * @brief Constructor with magic
 */
CUnknownMessage::CUnknownMessage(uint32_t n_magic)
    : CPeerMessage(n_magic) {
}

/**
 * @brief Get message type
 */
std::string CUnknownMessage::GetType() const {
    return MessageType::UNKNOWN;
}

/**
 * @brief Serialize empty payload
 */
std::vector<uint8_t> CUnknownMessage::SerializePayload() const {
    LOG_TRACE("CUnknownMessage::SerializePayload - Starting serialization");
    LOG_TRACE("CUnknownMessage::SerializePayload - Success: 0 bytes (empty payload)");
    return std::vector<uint8_t>();  // Empty payload
}

/**
 * @brief Deserialize payload (always fails - unknown messages are invalid)
 */
bool CUnknownMessage::DeserializePayload(const std::vector<uint8_t>& payload) {
    // Validation: Unknown messages are never valid
    // Suppress warning
    (void)payload;
    return false;
}

/**
 * @brief Create a copy of this message
 */
std::unique_ptr<CPeerMessage> CUnknownMessage::Clone() const {
    return std::make_unique<CUnknownMessage>(GetMagic());
}


/**
 * @brief Get payload size (always returns 0)
 */
size_t CUnknownMessage::GetPayloadSize() const {
    return 0;
}
