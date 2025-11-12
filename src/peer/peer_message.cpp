// ============= peer_message.cpp =============
#include "peer_message.h"
#include <cstring>
// Network byte order functions are in peer_message.h (platform-specific)

// ============= MessageType Namespace Functions =============

/**
 * @brief Check if a message type string is valid
 */
bool MessageType::IsValid(const std::string& str_type) {
    return str_type == PING ||
           str_type == PONG ||
           str_type == GET_PEERS ||
           str_type == PEERS ||
           str_type == TX ||
           str_type == BLOCK ||
           str_type == GET_CHAIN ||
           str_type == CHAIN_INFO ||
           str_type == INVENTORY ||
           str_type == VERSION ||
           str_type == GETDATA;
}

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
CPeerMessage::CPeerMessage() : m_str_type(MessageType::UNKNOWN) {
}

/**
 * @brief Construct message with specific type
 */
CPeerMessage::CPeerMessage(const std::string& str_type) : m_str_type(str_type) {
}

/**
 * @brief Construct message with type and string payload
 */
CPeerMessage::CPeerMessage(const std::string& str_type, const std::string& str_payload)
    : m_str_type(str_type) {
    SetPayload(str_payload);
}

/**
 * @brief Construct message with type and binary payload
 */
CPeerMessage::CPeerMessage(const std::string& str_type, const std::vector<uint8_t>& payload)
    : m_str_type(str_type), m_payload(payload) {
}

// ============= Getters and Setters =============

/**
 * @brief Get message type
 */
const std::string& CPeerMessage::GetType() const {
    return m_str_type;
}

/**
 * @brief Set message type
 */
void CPeerMessage::SetType(const std::string& str_type) {
    m_str_type = str_type;
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
 * @return Serialized message in format: [1 byte type_length][N bytes type][4 bytes payload_length][M bytes payload]
 *
 * The length fields are in network byte order (big-endian) for platform independence.
 */
std::string CPeerMessage::Serialize() const {
    std::string str_result;

    // Calculate total size
    uint8_t n_type_length = static_cast<uint8_t>(m_str_type.length());
    size_t n_total_size = 1 + n_type_length + 4 + m_payload.size();
    str_result.reserve(n_total_size);

    // 1. Add type length (1 byte)
    str_result.push_back(static_cast<char>(n_type_length));

    // 2. Add type string (N bytes)
    str_result.append(m_str_type);

    // 3. Add payload length (4 bytes, network byte order)
    uint32_t n_payload_length = static_cast<uint32_t>(m_payload.size());
    uint32_t n_length_network = HostToNetwork(n_payload_length);

    const char* p_length = reinterpret_cast<const char*>(&n_length_network);
    str_result.append(p_length, 4);

    // 4. Add payload data
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
 * Parses message format: [1 byte type_length][N bytes type][4 bytes payload_length][M bytes payload]
 * Validates that the data contains enough bytes for the type and payload.
 */
bool CPeerMessage::Deserialize(const std::string& str_data) {
    // Need at least 1 byte for type length
    if (str_data.size() < 1) {
        return false;
    }

    size_t n_offset = 0;

    // 1. Parse type length (1 byte)
    uint8_t n_type_length = static_cast<uint8_t>(str_data[n_offset]);
    n_offset += 1;

    // 2. Check if we have enough data for type string
    if (str_data.size() < n_offset + n_type_length) {
        return false;
    }

    // 3. Parse type string (N bytes)
    m_str_type = str_data.substr(n_offset, n_type_length);
    n_offset += n_type_length;

    // 4. Check if we have enough data for payload length
    if (str_data.size() < n_offset + 4) {
        return false;
    }

    // 5. Parse payload length (4 bytes, network byte order)
    uint32_t n_length_network;
    std::memcpy(&n_length_network, str_data.data() + n_offset, 4);
    uint32_t n_payload_length = NetworkToHost(n_length_network);
    n_offset += 4;

    // 6. Validate that we have enough data for the payload
    if (str_data.size() < n_offset + n_payload_length) {
        return false;
    }

    // 7. Extract payload
    m_payload.clear();
    if (n_payload_length > 0) {
        const uint8_t* p_payload = reinterpret_cast<const uint8_t*>(str_data.data() + n_offset);
        m_payload.assign(p_payload, p_payload + n_payload_length);
    }

    return true;
}

// ============= Validation =============

/**
 * @brief Check if message is valid
 * @return true if message has valid type string
 */
bool CPeerMessage::IsValid() const {
    return MessageType::IsValid(m_str_type);
}

// ============= Utility Functions =============

/**
 * @brief Get string representation of this message's type
 */
std::string CPeerMessage::GetTypeString() const {
    return m_str_type;
}
