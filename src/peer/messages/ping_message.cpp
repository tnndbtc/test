// ============= ping_message.cpp =============
#include "ping_message.h"
#include "logger/logger.h"
#include <cstring>
#include <cstdlib>
#include "utils/generate_nonce.h"

// Include network byte order functions
#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <arpa/inet.h>
#endif

/**
 * @brief Constructor with magic - auto-generates random nonce
 */
CPingMessage::CPingMessage(uint32_t n_magic)
    : CPeerMessage(n_magic), m_n_nonce(0) {
    m_n_nonce = GenerateNonce();
}

/**
 * @brief Get message type
 */
std::string CPingMessage::GetType() const {
    return MessageType::PING;
}

/**
 * @brief Serialize nonce to 4-byte big-endian payload
 */
std::vector<uint8_t> CPingMessage::SerializePayload() const {
    LOG_TRACE("CPingMessage::SerializePayload - Starting serialization");
    std::vector<uint8_t> payload(4);

    // Convert nonce to network byte order (big-endian)
    uint32_t n_nonce_network = htonl(m_n_nonce);
    std::memcpy(payload.data(), &n_nonce_network, 4);

    LOG_TRACE("CPingMessage::SerializePayload - Success: " + std::to_string(payload.size()) + " bytes");
    return payload;
}

/**
 * @brief Deserialize 4-byte big-endian payload to nonce
 */
bool CPingMessage::DeserializePayload(const std::vector<uint8_t>& payload) {
    // PING payload must be exactly 4 bytes
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
std::unique_ptr<CPeerMessage> CPingMessage::Clone() const {
    auto p_clone = std::make_unique<CPingMessage>(GetMagic());
    p_clone->m_n_nonce = m_n_nonce;  // Copy the nonce value
    return p_clone;
}


/**
 * @brief Get nonce value
 */
uint32_t CPingMessage::GetNonce() const {
    return m_n_nonce;
}
