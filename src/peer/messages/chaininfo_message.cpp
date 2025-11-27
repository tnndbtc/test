// ============= chaininfo_message.cpp =============
#include "chaininfo_message.h"
#include "logger/logger.h"
#include <cstring>

/**
 * @brief Default constructor with magic
 */
CChainInfoMessage::CChainInfoMessage(uint32_t n_magic)
    : CPeerMessage(n_magic), m_n_height(0), m_str_best_block() {
}

/**
 * @brief Constructor with chain data and magic
 */
CChainInfoMessage::CChainInfoMessage(uint64_t n_height, const std::string& str_best_block, uint32_t n_magic)
    : CPeerMessage(n_magic), m_n_height(n_height), m_str_best_block(str_best_block) {
}

/**
 * @brief Get message type
 */
std::string CChainInfoMessage::GetType() const {
    return MessageType::CHAIN_INFO;
}

/**
 * @brief Serialize chain info to binary payload
 * Format: [height:8bytes][best_block_length:4bytes][best_block:N bytes]
 */
std::vector<uint8_t> CChainInfoMessage::SerializePayload() const {
    std::vector<uint8_t> payload;

    // Serialize height (8 bytes, network byte order)
    uint64_t n_height_network = HostToNetwork64(m_n_height);
    const uint8_t* p_height = reinterpret_cast<const uint8_t*>(&n_height_network);
    payload.insert(payload.end(), p_height, p_height + 8);

    // Serialize best block hash length (4 bytes, network byte order)
    uint32_t n_hash_length = static_cast<uint32_t>(m_str_best_block.length());
    uint32_t n_length_network = HostToNetwork(n_hash_length);
    const uint8_t* p_length = reinterpret_cast<const uint8_t*>(&n_length_network);
    payload.insert(payload.end(), p_length, p_length + 4);

    // Serialize best block hash string
    if (n_hash_length > 0) {
    LOG_TRACE("CChainInfoMessage::SerializePayload - Starting serialization");
        payload.insert(payload.end(), m_str_best_block.begin(), m_str_best_block.end());
    }

    LOG_TRACE("CChainInfoMessage::SerializePayload - Success: " + std::to_string(payload.size()) + " bytes");
    return payload;
}

/**
 * @brief Deserialize binary payload to chain info
 */
bool CChainInfoMessage::DeserializePayload(const std::vector<uint8_t>& payload) {
    // Need at least 12 bytes (8 for height + 4 for hash length)
    if (payload.size() < 12) {
        return false;
    }

    size_t n_offset = 0;

    // Deserialize height (8 bytes)
    uint64_t n_height_network;
    std::memcpy(&n_height_network, payload.data() + n_offset, 8);
    m_n_height = NetworkToHost64(n_height_network);
    n_offset += 8;

    // Deserialize best block hash length (4 bytes)
    uint32_t n_length_network;
    std::memcpy(&n_length_network, payload.data() + n_offset, 4);
    uint32_t n_hash_length = NetworkToHost(n_length_network);
    n_offset += 4;

    // Validate that we have enough data for the hash string
    if (payload.size() < n_offset + n_hash_length) {
        return false;
    }

    // Deserialize best block hash string
    if (n_hash_length > 0) {
        m_str_best_block.assign(payload.begin() + n_offset, payload.begin() + n_offset + n_hash_length);
    } else {
        m_str_best_block.clear();
    }

    return true;
}

/**
 * @brief Create a copy of this message
 */
std::unique_ptr<CPeerMessage> CChainInfoMessage::Clone() const {
    return std::make_unique<CChainInfoMessage>(m_n_height, m_str_best_block, GetMagic());
}


/**
 * @brief Get blockchain height
 */
uint64_t CChainInfoMessage::GetHeight() const {
    return m_n_height;
}

/**
 * @brief Get best block hash
 */
const std::string& CChainInfoMessage::GetBestBlock() const {
    return m_str_best_block;
}

/**
 * @brief Set blockchain height
 */
void CChainInfoMessage::SetHeight(uint64_t n_height) {
    m_n_height = n_height;
}

/**
 * @brief Set best block hash
 */
void CChainInfoMessage::SetBestBlock(const std::string& str_best_block) {
    m_str_best_block = str_best_block;
}

/**
 * @brief Helper: Convert uint64_t to network byte order
 */
uint64_t CChainInfoMessage::HostToNetwork64(uint64_t n_value) {
    // For 64-bit values, we need to handle byte order manually
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return ((uint64_t)htonl(n_value & 0xFFFFFFFF) << 32) | htonl(n_value >> 32);
    #else
        return n_value;
    #endif
}

/**
 * @brief Helper: Convert uint64_t from network byte order
 */
uint64_t CChainInfoMessage::NetworkToHost64(uint64_t n_value) {
    // For 64-bit values, we need to handle byte order manually
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return ((uint64_t)ntohl(n_value & 0xFFFFFFFF) << 32) | ntohl(n_value >> 32);
    #else
        return n_value;
    #endif
}
