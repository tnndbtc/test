// ============= peer_message.cpp =============
#include "peer_message.h"
#include <cstring>
#include <arpa/inet.h>

// ============= Helper Functions =============

/**
 * @brief Convert uint32_t to network byte order (big-endian)
 */
uint32_t CPeerMessage::HostToNetwork(uint32_t n_value) {
    return htonl(n_value);
}

/**
 * @brief Convert uint32_t from network byte order to host byte order
 */
uint32_t CPeerMessage::NetworkToHost(uint32_t n_value) {
    return ntohl(n_value);
}

// ============= Constructors =============

/**
 * @brief Default constructor - creates UNKNOWN message
 */
CPeerMessage::CPeerMessage() : m_type(EMessageType::UNKNOWN) {
}

/**
 * @brief Construct message with specific type
 */
CPeerMessage::CPeerMessage(EMessageType type) : m_type(type) {
}

/**
 * @brief Construct message with type and string payload
 */
CPeerMessage::CPeerMessage(EMessageType type, const std::string& str_payload)
    : m_type(type) {
    SetPayload(str_payload);
}

/**
 * @brief Construct message with type and binary payload
 */
CPeerMessage::CPeerMessage(EMessageType type, const std::vector<uint8_t>& payload)
    : m_type(type), m_payload(payload) {
}

// ============= Getters and Setters =============

/**
 * @brief Get message type
 */
EMessageType CPeerMessage::GetType() const {
    return m_type;
}

/**
 * @brief Set message type
 */
void CPeerMessage::SetType(EMessageType type) {
    m_type = type;
}

/**
 * @brief Get payload as string
 */
std::string CPeerMessage::GetPayloadString() const {
    return std::string(m_payload.begin(), m_payload.end());
}

/**
 * @brief Get payload as byte vector
 */
const std::vector<uint8_t>& CPeerMessage::GetPayloadBytes() const {
    return m_payload;
}

/**
 * @brief Set payload from string
 */
void CPeerMessage::SetPayload(const std::string& str_payload) {
    m_payload.assign(str_payload.begin(), str_payload.end());
}

/**
 * @brief Set payload from byte vector
 */
void CPeerMessage::SetPayload(const std::vector<uint8_t>& payload) {
    m_payload = payload;
}

/**
 * @brief Get payload size in bytes
 */
size_t CPeerMessage::GetPayloadSize() const {
    return m_payload.size();
}

// ============= Serialization =============

/**
 * @brief Serialize message to byte string for transmission
 * @return Serialized message in format: [1 byte type][4 bytes length][N bytes payload]
 *
 * The length field is in network byte order (big-endian) for platform independence.
 */
std::string CPeerMessage::Serialize() const {
    std::string str_result;

    // Reserve space for header + payload
    str_result.reserve(GetHeaderSize() + m_payload.size());

    // 1. Add message type (1 byte)
    str_result.push_back(static_cast<char>(m_type));

    // 2. Add payload length (4 bytes, network byte order)
    uint32_t n_length = static_cast<uint32_t>(m_payload.size());
    uint32_t n_length_network = HostToNetwork(n_length);

    const char* p_length = reinterpret_cast<const char*>(&n_length_network);
    str_result.append(p_length, 4);

    // 3. Add payload data
    if (!m_payload.empty()) {
        str_result.append(reinterpret_cast<const char*>(m_payload.data()), m_payload.size());
    }

    return str_result;
}

/**
 * @brief Deserialize message from byte string
 * @param str_data Serialized message data
 * @return true if deserialization successful, false if data is invalid
 *
 * Parses message format: [1 byte type][4 bytes length][N bytes payload]
 * Validates that the data contains enough bytes for the payload.
 */
bool CPeerMessage::Deserialize(const std::string& str_data) {
    // Need at least header (5 bytes: 1 type + 4 length)
    if (str_data.size() < GetHeaderSize()) {
        return false;
    }

    // 1. Parse message type (1 byte)
    m_type = static_cast<EMessageType>(static_cast<uint8_t>(str_data[0]));

    // 2. Parse payload length (4 bytes, network byte order)
    uint32_t n_length_network;
    std::memcpy(&n_length_network, str_data.data() + 1, 4);
    uint32_t n_length = NetworkToHost(n_length_network);

    // 3. Validate that we have enough data for the payload
    size_t n_expected_size = GetHeaderSize() + n_length;
    if (str_data.size() < n_expected_size) {
        return false;
    }

    // 4. Extract payload
    m_payload.clear();
    if (n_length > 0) {
        const uint8_t* p_payload = reinterpret_cast<const uint8_t*>(str_data.data() + GetHeaderSize());
        m_payload.assign(p_payload, p_payload + n_length);
    }

    return true;
}

// ============= Validation =============

/**
 * @brief Check if message is valid
 * @return true if message has non-UNKNOWN type
 */
bool CPeerMessage::IsValid() const {
    return m_type != EMessageType::UNKNOWN;
}

// ============= Utility Functions =============

/**
 * @brief Get string representation of message type
 */
std::string CPeerMessage::TypeToString(EMessageType type) {
    switch (type) {
        case EMessageType::PING:       return "PING";
        case EMessageType::PONG:       return "PONG";
        case EMessageType::GET_PEERS:  return "GET_PEERS";
        case EMessageType::PEERS:      return "PEERS";
        case EMessageType::TX_IDS:     return "TX_IDS";
        case EMessageType::GET_TX:     return "GET_TX";
        case EMessageType::TX:         return "TX";
        case EMessageType::GET_BLOCK:  return "GET_BLOCK";
        case EMessageType::BLOCK:      return "BLOCK";
        case EMessageType::GET_CHAIN:  return "GET_CHAIN";
        case EMessageType::CHAIN_INFO: return "CHAIN_INFO";
        case EMessageType::UNKNOWN:    return "UNKNOWN";
        default:                       return "INVALID";
    }
}

/**
 * @brief Get string representation of this message's type
 */
std::string CPeerMessage::GetTypeString() const {
    return TypeToString(m_type);
}
