// ============= version_message.cpp =============
#include "version_message.h"
#include "blockcore/protocol.h"
#include "utils/settings.h"
#include "utils/generate_nonce.h"
#include <cstring>
#include <sstream>
#include <ctime>

// Platform-specific byte order conversion
#ifdef __APPLE__
    #include <machine/endian.h>
    #include <libkern/OSByteOrder.h>
    #include <arpa/inet.h>
    #define htobe64(x) OSSwapHostToBigInt64(x)
    #define be64toh(x) OSSwapBigToHostInt64(x)
#elif defined(__linux__)
    #include <endian.h>
    #include <arpa/inet.h>
#elif defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define htobe64(x) htonll(x)
    #define be64toh(x) ntohll(x)
#endif

/**
 * @brief Default constructor for CNetworkAddress
 */
CNetworkAddress::CNetworkAddress()
    : n_services(0), n_port(0) {
    address.fill(0);
}

/**
 * @brief Constructor for CNetworkAddress with services, IP, and port
 */
CNetworkAddress::CNetworkAddress(uint64_t services, const std::string& str_ip, uint16_t port)
    : n_services(services), n_port(port) {
    address.fill(0);

    // Try to parse as IPv4 first
    struct in_addr ipv4_addr;
    if (inet_pton(AF_INET, str_ip.c_str(), &ipv4_addr) == 1) {
        // IPv4-mapped IPv6 address format: ::ffff:x.x.x.x
        address[10] = 0xff;
        address[11] = 0xff;
        std::memcpy(&address[12], &ipv4_addr, 4);
    } else {
        // Try to parse as IPv6
        struct in6_addr ipv6_addr;
        if (inet_pton(AF_INET6, str_ip.c_str(), &ipv6_addr) == 1) {
            std::memcpy(address.data(), &ipv6_addr, 16);
        }
        // If both fail, address remains all zeros
    }
}

/**
 * @brief Constructor with magic
 * Auto-initializes protocol_version from PROTOCOL_VERSION constant,
 * timestamp to current UTC time, and nonce to random value
 */
CVersionMessage::CVersionMessage(uint32_t n_magic)
    : CPeerMessage(n_magic),
      m_n_protocol_version(PROTOCOL_VERSION),
      m_n_timestamp(static_cast<int64_t>(std::time(nullptr))),
      m_n_nonce(0),
      m_n_chain_tip_height(0) {
    // Generate random nonce
    m_n_nonce = GenerateNonce();
}

/**
 * @brief Get message type
 */
std::string CVersionMessage::GetType() const {
    return MessageType::VERSION;
}

/**
 * @brief Serialize VERSION message to binary payload
 *
 * Payload format (76 bytes total):
 * - protocol_version: 4 bytes (int32_t)
 * - timestamp: 8 bytes (int64_t)
 * - address_recv: 26 bytes (8 services + 16 address + 2 port)
 * - address_from: 26 bytes (8 services + 16 address + 2 port)
 * - nonce: 8 bytes (uint64_t)
 * - chain_tip_height: 4 bytes (int32_t)
 */
std::vector<uint8_t> CVersionMessage::SerializePayload() const {
    std::vector<uint8_t> payload;
    payload.resize(76);  // Pre-allocate exact size needed
    size_t offset = 0;

    // Serialize protocol_version (4 bytes)
    uint32_t n_protocol_version_network = htonl(static_cast<uint32_t>(m_n_protocol_version));
    std::memcpy(payload.data() + offset, &n_protocol_version_network, 4);
    offset += 4;

    // Serialize timestamp (8 bytes)
    uint64_t n_timestamp_network = htobe64(static_cast<uint64_t>(m_n_timestamp));
    std::memcpy(payload.data() + offset, &n_timestamp_network, 8);
    offset += 8;

    // Serialize address_recv (26 bytes)
    uint64_t n_recv_services_network = htobe64(m_address_recv.n_services);
    std::memcpy(payload.data() + offset, &n_recv_services_network, 8);
    offset += 8;
    std::memcpy(payload.data() + offset, m_address_recv.address.data(), 16);
    offset += 16;
    uint16_t n_recv_port_network = htons(m_address_recv.n_port);
    std::memcpy(payload.data() + offset, &n_recv_port_network, 2);
    offset += 2;

    // Serialize address_from (26 bytes)
    uint64_t n_from_services_network = htobe64(m_address_from.n_services);
    std::memcpy(payload.data() + offset, &n_from_services_network, 8);
    offset += 8;
    std::memcpy(payload.data() + offset, m_address_from.address.data(), 16);
    offset += 16;
    uint16_t n_from_port_network = htons(m_address_from.n_port);
    std::memcpy(payload.data() + offset, &n_from_port_network, 2);
    offset += 2;

    // Serialize nonce (8 bytes)
    uint64_t n_nonce_network = htobe64(m_n_nonce);
    std::memcpy(payload.data() + offset, &n_nonce_network, 8);
    offset += 8;

    // Serialize chain_tip_height (4 bytes)
    uint32_t n_chain_tip_height_network = htonl(static_cast<uint32_t>(m_n_chain_tip_height));
    std::memcpy(payload.data() + offset, &n_chain_tip_height_network, 4);
    offset += 4;

    return payload;
}

/**
 * @brief Deserialize binary payload to VERSION message
 */
bool CVersionMessage::DeserializePayload(const std::vector<uint8_t>& payload) {
    // VERSION payload must be exactly 76 bytes
    if (payload.size() != 76) {
        return false;
    }

    size_t offset = 0;

    // Deserialize protocol_version (4 bytes)
    uint32_t n_protocol_version_network;
    std::memcpy(&n_protocol_version_network, payload.data() + offset, 4);
    m_n_protocol_version = static_cast<int32_t>(ntohl(n_protocol_version_network));
    offset += 4;

    // Deserialize timestamp (8 bytes)
    uint64_t n_timestamp_network;
    std::memcpy(&n_timestamp_network, payload.data() + offset, 8);
    m_n_timestamp = static_cast<int64_t>(be64toh(n_timestamp_network));
    offset += 8;

    // Deserialize address_recv (26 bytes)
    uint64_t n_recv_services_network;
    std::memcpy(&n_recv_services_network, payload.data() + offset, 8);
    m_address_recv.n_services = be64toh(n_recv_services_network);
    offset += 8;
    std::memcpy(m_address_recv.address.data(), payload.data() + offset, 16);
    offset += 16;
    uint16_t n_recv_port_network;
    std::memcpy(&n_recv_port_network, payload.data() + offset, 2);
    m_address_recv.n_port = ntohs(n_recv_port_network);
    offset += 2;

    // Deserialize address_from (26 bytes)
    uint64_t n_from_services_network;
    std::memcpy(&n_from_services_network, payload.data() + offset, 8);
    m_address_from.n_services = be64toh(n_from_services_network);
    offset += 8;
    std::memcpy(m_address_from.address.data(), payload.data() + offset, 16);
    offset += 16;
    uint16_t n_from_port_network;
    std::memcpy(&n_from_port_network, payload.data() + offset, 2);
    m_address_from.n_port = ntohs(n_from_port_network);
    offset += 2;

    // Deserialize nonce (8 bytes)
    uint64_t n_nonce_network;
    std::memcpy(&n_nonce_network, payload.data() + offset, 8);
    m_n_nonce = be64toh(n_nonce_network);
    offset += 8;

    // Deserialize chain_tip_height (4 bytes)
    uint32_t n_chain_tip_height_network;
    std::memcpy(&n_chain_tip_height_network, payload.data() + offset, 4);
    m_n_chain_tip_height = static_cast<int32_t>(ntohl(n_chain_tip_height_network));
    offset += 4;

    return true;
}

/**
 * @brief Create a copy of this message
 */
std::unique_ptr<CPeerMessage> CVersionMessage::Clone() const {
    auto p_clone = std::make_unique<CVersionMessage>(GetMagic());
    p_clone->m_n_protocol_version = m_n_protocol_version;
    p_clone->m_n_timestamp = m_n_timestamp;
    p_clone->m_address_recv = m_address_recv;
    p_clone->m_address_from = m_address_from;
    p_clone->m_n_nonce = m_n_nonce;
    p_clone->m_n_chain_tip_height = m_n_chain_tip_height;
    return p_clone;
}

// Getter and setter methods

int32_t CVersionMessage::GetProtocolVersion() const {
    return m_n_protocol_version;
}

int64_t CVersionMessage::GetTimestamp() const {
    return m_n_timestamp;
}

const CNetworkAddress& CVersionMessage::GetAddressRecv() const {
    return m_address_recv;
}

void CVersionMessage::SetAddressRecv(const CNetworkAddress& address) {
    m_address_recv = address;
}

const CNetworkAddress& CVersionMessage::GetAddressFrom() const {
    return m_address_from;
}

void CVersionMessage::SetAddressFrom(const CNetworkAddress& address) {
    m_address_from = address;
}

uint64_t CVersionMessage::GetNonce() const {
    return m_n_nonce;
}

int32_t CVersionMessage::GetChainTipHeight() const {
    return m_n_chain_tip_height;
}

void CVersionMessage::SetChainTipHeight(int32_t n_height) {
    m_n_chain_tip_height = n_height;
}
