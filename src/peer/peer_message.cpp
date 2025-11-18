// ============= peer_message.cpp =============
#include "peer_message.h"
#include "logger/logger.h"
#include <cstring>
#include <iomanip>
#include <sstream>
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
 * @brief Constructor - initializes magic bytes
 */
CPeerMessage::CPeerMessage(uint32_t n_magic)
    : m_n_magic(n_magic) {
}

// ============= Getters and Setters =============

/**
 * @brief Get network magic bytes
 */
uint32_t CPeerMessage::GetMagic() const {
    return m_n_magic;
}

/**
 * @brief Set network magic bytes
 */
void CPeerMessage::SetMagic(uint32_t n_magic) {
    m_n_magic = n_magic;
}

// ============= Serialization =============

/**
 * @brief Serialize message to byte string for transmission
 * @return Serialized message in format: [4 bytes magic][1 byte type_length][N bytes type][4 bytes payload_length][M bytes payload]
 *
 * The length fields are in network byte order (big-endian) for platform independence.
 */
std::string CPeerMessage::Serialize() const {
    std::string str_result;

    // Get message type and payload from derived class
    std::string str_type = GetType();
    std::vector<uint8_t> payload = SerializePayload();

    // Calculate total size
    uint8_t n_type_length = static_cast<uint8_t>(str_type.length());
    size_t n_total_size = 4 + 1 + n_type_length + 4 + payload.size();
    str_result.reserve(n_total_size);

    // 1. Add magic bytes (4 bytes, network byte order)
    uint32_t n_magic_network = HostToNetwork(m_n_magic);
    const char* p_magic = reinterpret_cast<const char*>(&n_magic_network);
    str_result.append(p_magic, 4);

    // 2. Add type length (1 byte)
    str_result.push_back(static_cast<char>(n_type_length));

    // 3. Add type string (N bytes)
    str_result.append(str_type);

    // 4. Add payload length (4 bytes, network byte order)
    uint32_t n_payload_length = static_cast<uint32_t>(payload.size());
    uint32_t n_length_network = HostToNetwork(n_payload_length);

    const char* p_length = reinterpret_cast<const char*>(&n_length_network);
    str_result.append(p_length, 4);

    // 5. Add payload data
    if (!payload.empty()) {
        str_result.append(reinterpret_cast<const char*>(payload.data()), payload.size());
    }

    // Log serialization at trace level
    LOG_TRACE("Serialized P2P message: type='" + str_type + "', total_size=" + std::to_string(n_total_size) + " bytes");

    return str_result;
}

/**
 * @brief Deserialize message from byte string with magic validation
 * @param str_data Serialized message data
 * @param n_expected_magic Expected network magic bytes (0 = don't validate)
 * @return true if deserialization successful and magic matches, false if data is invalid
 *
 * Parses message format: [4 bytes magic][1 byte type_length][N bytes type][4 bytes payload_length][M bytes payload]
 * Validates that the data contains enough bytes for the type and payload, and magic matches if specified.
 *
 * NOTE: This base class implementation only parses the wire format header (magic, type, payload length).
 * It extracts the payload and calls the derived class's DeserializePayload() method to parse message-specific data.
 * This should NOT be called directly - use CPeerMessageFactory::CreateFromWireData() instead.
 */
bool CPeerMessage::Deserialize(const std::string& str_data, uint32_t n_expected_magic) {
    // Need at least 4 bytes for magic
    if (str_data.size() < 4) {
        return false;
    }

    size_t n_offset = 0;

    // 1. Parse magic bytes (4 bytes, network byte order)
    uint32_t n_magic_network;
    std::memcpy(&n_magic_network, str_data.data() + n_offset, 4);
    m_n_magic = NetworkToHost(n_magic_network);
    n_offset += 4;

    // 2. Validate magic if expected magic is provided (non-zero)
    if (n_expected_magic != 0 && m_n_magic != n_expected_magic) {
        // Log magic mismatch - indicates message from different network
        std::ostringstream oss;
        oss << "Network magic mismatch: received 0x" << std::hex << std::setw(8) << std::setfill('0') << m_n_magic
            << ", expected 0x" << std::hex << std::setw(8) << std::setfill('0') << n_expected_magic
            << " (message from wrong network)";
        LOG_WARN(oss.str());
        return false;  // Magic mismatch - wrong network
    }

    // 3. Check if we have enough data for type length
    if (str_data.size() < n_offset + 1) {
        LOG_WARN("No enough data for type length");
        return false;
    }

    // 4. Parse type length (1 byte) - we don't use this in base class, just skip it
    uint8_t n_type_length = static_cast<uint8_t>(str_data[n_offset]);
    n_offset += 1;

    // 5. Check if we have enough data for type string
    if (str_data.size() < n_offset + n_type_length) {
        LOG_WARN("No enough data for type string");
        return false;
    }

    // 6. Skip type string (N bytes) - derived class already knows its type
    std::string str_parsed_type = str_data.substr(n_offset, n_type_length);
    n_offset += n_type_length;

    // 7. Check if we have enough data for payload length
    if (str_data.size() < n_offset + 4) {
        LOG_WARN("No enough payload length");
        return false;
    }

    // 8. Parse payload length (4 bytes, network byte order)
    uint32_t n_length_network;
    std::memcpy(&n_length_network, str_data.data() + n_offset, 4);
    uint32_t n_payload_length = NetworkToHost(n_length_network);
    n_offset += 4;

    // 9. Validate that we have enough data for the payload
    if (str_data.size() < n_offset + n_payload_length) {
        LOG_WARN("No enough payload data");
        return false;
    }

    // 10. Extract payload and pass to derived class
    std::vector<uint8_t> payload;
    if (n_payload_length > 0) {
        const uint8_t* p_payload = reinterpret_cast<const uint8_t*>(str_data.data() + n_offset);
        payload.assign(p_payload, p_payload + n_payload_length);
    }

    // Call derived class to parse payload
    if (!DeserializePayload(payload)) {
        LOG_WARN("DeserializePayload failed");
        return false;
    }

    // Log successful deserialization at trace level
    LOG_TRACE("Deserialized P2P message: type='" + str_parsed_type + "', payload_size=" + std::to_string(n_payload_length) + " bytes");

    return true;
}

// IsValid() is now a virtual method with default implementation in the header
// Derived classes can override if needed
