// ============= pong_message.cpp =============
#include "pong_message.h"
#include <cstring>

// Include network byte order functions
#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <arpa/inet.h>
#endif

/**
 * @brief Constructor with nonce and magic
 */
CPongMessage::CPongMessage(uint32_t n_nonce, uint32_t n_magic)
    : CPeerMessage(n_magic), m_n_nonce(n_nonce) {
}

/**
 * @brief Get message type
 */
std::string CPongMessage::GetType() const {
    return MessageType::PONG;
}

/**
 * @brief Serialize nonce to 4-byte big-endian payload
 */
std::vector<uint8_t> CPongMessage::SerializePayload() const {
    std::vector<uint8_t> payload(4);

    // Convert nonce to network byte order (big-endian)
    uint32_t n_nonce_network = htonl(m_n_nonce);
    std::memcpy(payload.data(), &n_nonce_network, 4);

    return payload;
}

/**
 * @brief Deserialize 4-byte big-endian payload to nonce
 */
bool CPongMessage::DeserializePayload(const std::vector<uint8_t>& payload) {
    // PONG payload must be exactly 4 bytes
    if (payload.size() != 4) {
        return false;
    }

    // Convert from network byte order (big-endian) to host byte order
    uint32_t n_nonce_network;
    std::memcpy(&n_nonce_network, payload.data(), 4);
    m_n_nonce = ntohl(n_nonce_network);

    return true;
}

/**
 * @brief Create a copy of this message
 */
std::unique_ptr<CPeerMessage> CPongMessage::Clone() const {
    return std::make_unique<CPongMessage>(m_n_nonce, GetMagic());
}


/**
 * @brief Get nonce value
 */
uint32_t CPongMessage::GetNonce() const {
    return m_n_nonce;
}

/**
 * @brief Set nonce value
 */
void CPongMessage::SetNonce(uint32_t n_nonce) {
    m_n_nonce = n_nonce;
}
