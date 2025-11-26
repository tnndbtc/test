// ============= block_message.cpp =============
#include "block_message.h"
#include "utils/settings.h"
#include <cstring>

/**
 * @brief Constructor with block and magic
 */
CBlockMessage::CBlockMessage(std::shared_ptr<CBlock> p_block, uint32_t n_magic)
    : CPeerMessage(n_magic), m_p_block(p_block) {
}

/**
 * @brief Get message type
 */
std::string CBlockMessage::GetType() const {
    return MessageType::BLOCK;
}

/**
 * @brief Serialize block data to binary payload
 */
std::vector<uint8_t> CBlockMessage::SerializePayload() const {
    if (!m_p_block) {
        return std::vector<uint8_t>();  // Empty payload for null block
    }
    std::string str_serialized = m_p_block->Serialize();
    std::vector<uint8_t> payload(str_serialized.begin(), str_serialized.end());
    return payload;
}

/**
 * @brief Deserialize binary payload to block data
 */
bool CBlockMessage::DeserializePayload(const std::vector<uint8_t>& payload) {
    // Validation: Block data must not be empty
    if (payload.empty()) {
        return false;
    }

    std::string str_serialized(reinterpret_cast<const char*>(payload.data()), payload.size());
    m_p_block = CBlock::Deserialize(str_serialized);

    // Validation: Deserialization must succeed
    if (!m_p_block) {
        return false;
    }

    return true;
}

/**
 * @brief Create a copy of this message
 */
std::unique_ptr<CPeerMessage> CBlockMessage::Clone() const {
    return std::make_unique<CBlockMessage>(m_p_block, GetMagic());
}


/**
 * @brief Get block object
 */
std::shared_ptr<CBlock> CBlockMessage::GetBlock() const {
    return m_p_block;
}
