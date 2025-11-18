// ============= version_message.cpp =============
#include "version_message.h"
#include "utils/settings.h"
#include <cstring>

/**
 * @brief Constructor with version info and magic
 */
CVersionMessage::CVersionMessage(const std::string& str_version_info, uint32_t n_magic)
    : CPeerMessage(n_magic), m_str_version_info(str_version_info) {
}

/**
 * @brief Get message type
 */
std::string CVersionMessage::GetType() const {
    return MessageType::VERSION;
}

/**
 * @brief Serialize version info to binary payload
 */
std::vector<uint8_t> CVersionMessage::SerializePayload() const {
    std::vector<uint8_t> payload(m_str_version_info.size());
    std::memcpy(payload.data(), m_str_version_info.data(), m_str_version_info.size());
    return payload;
}

/**
 * @brief Deserialize binary payload to version info
 */
bool CVersionMessage::DeserializePayload(const std::vector<uint8_t>& payload) {
    m_str_version_info = std::string(reinterpret_cast<const char*>(payload.data()), payload.size());
    return true;
}

/**
 * @brief Create a copy of this message
 */
std::unique_ptr<CPeerMessage> CVersionMessage::Clone() const {
    return std::make_unique<CVersionMessage>(m_str_version_info, GetMagic());
}


/**
 * @brief Get version information string
 */
const std::string& CVersionMessage::GetVersionInfo() const {
    return m_str_version_info;
}

/**
 * @brief Set version information string
 */
void CVersionMessage::SetVersionInfo(const std::string& str_version_info) {
    m_str_version_info = str_version_info;
}
