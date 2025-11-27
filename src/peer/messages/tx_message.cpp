// ============= tx_message.cpp =============
#include "tx_message.h"
#include "utils/settings.h"
#include "logger/logger.h"
#include <cstring>

/**
 * @brief Constructor with transaction and magic
 */
CTxMessage::CTxMessage(std::shared_ptr<CTransaction> p_tx, uint32_t n_magic)
    : CPeerMessage(n_magic), m_p_tx(p_tx) {
}

/**
 * @brief Get message type
 */
std::string CTxMessage::GetType() const {
    return MessageType::TX;
}

/**
 * @brief Serialize transaction data to binary payload
 */
std::vector<uint8_t> CTxMessage::SerializePayload() const {
    LOG_TRACE("CTxMessage::SerializePayload - Starting serialization");
    if (!m_p_tx) {
        LOG_TRACE("CTxMessage::SerializePayload - Success: Empty payload for null transaction");
        return std::vector<uint8_t>();  // Empty payload for null transaction
    }
    std::string str_serialized = m_p_tx->Serialize();
    std::vector<uint8_t> payload(str_serialized.begin(), str_serialized.end());
    LOG_TRACE("CTxMessage::SerializePayload - Success: " + std::to_string(payload.size()) + " bytes");
    return payload;
}

/**
 * @brief Deserialize binary payload to transaction data
 */
bool CTxMessage::DeserializePayload(const std::vector<uint8_t>& payload) {
    // Validation: Transaction data must not be empty
    if (payload.empty()) {
        return false;
    }

    std::string str_serialized(reinterpret_cast<const char*>(payload.data()), payload.size());
    m_p_tx = CTransaction::Deserialize(str_serialized);

    // Validation: Deserialization must succeed
    if (!m_p_tx) {
        return false;
    }

    return true;
}

/**
 * @brief Create a copy of this message
 */
std::unique_ptr<CPeerMessage> CTxMessage::Clone() const {
    return std::make_unique<CTxMessage>(m_p_tx, GetMagic());
}


/**
 * @brief Get transaction object
 */
std::shared_ptr<CTransaction> CTxMessage::GetTransaction() const {
    return m_p_tx;
}
