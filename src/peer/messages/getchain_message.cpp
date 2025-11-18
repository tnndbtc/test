// ============= getchain_message.cpp =============
#include "getchain_message.h"

/**
 * @brief Constructor with magic
 */
CGetChainMessage::CGetChainMessage(uint32_t n_magic)
    : CPeerMessage(n_magic) {
}

/**
 * @brief Get message type
 */
std::string CGetChainMessage::GetType() const {
    return MessageType::GET_CHAIN;
}

/**
 * @brief Serialize (empty payload)
 */
std::vector<uint8_t> CGetChainMessage::SerializePayload() const {
    return std::vector<uint8_t>();  // Empty payload
}

/**
 * @brief Deserialize (empty payload)
 */
bool CGetChainMessage::DeserializePayload(const std::vector<uint8_t>& payload) {
    // Accept any payload (including empty)
    // Suppress unused warning
    (void)payload;
    return true;
}

/**
 * @brief Create a copy of this message
 */
std::unique_ptr<CPeerMessage> CGetChainMessage::Clone() const {
    return std::make_unique<CGetChainMessage>(GetMagic());
}

