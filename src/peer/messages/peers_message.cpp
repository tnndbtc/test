// ============= peers_message.cpp =============
#include "peers_message.h"
#include <cstring>

// Include network byte order functions
#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <arpa/inet.h>
#endif

/**
 * @brief Constructor with magic
 */
CPeersMessage::CPeersMessage(uint32_t n_magic)
    : CPeerMessage(n_magic), m_vec_peers() {
}

/**
 * @brief Constructor with peers and magic
 */
CPeersMessage::CPeersMessage(const std::vector<CPeerInfo>& vec_peers, uint32_t n_magic)
    : CPeerMessage(n_magic), m_vec_peers(vec_peers) {
}

/**
 * @brief Get message type
 */
std::string CPeersMessage::GetType() const {
    return MessageType::PEERS;
}

/**
 * @brief Serialize peers to binary payload
 * Format: [count:4bytes][address_length:2bytes][address:N bytes][port:2bytes]...
 */
std::vector<uint8_t> CPeersMessage::SerializePayload() const {
    // Calculate total size
    size_t n_total_size = 4;  // 4 bytes for count
    for (const auto& peer : m_vec_peers) {
        n_total_size += 2 + peer.str_address.size() + 2;  // address_length + address + port
    }

    std::vector<uint8_t> payload(n_total_size);
    size_t n_offset = 0;

    // Write count (4 bytes, network byte order)
    uint32_t n_count = static_cast<uint32_t>(m_vec_peers.size());
    uint32_t n_count_network = htonl(n_count);
    std::memcpy(payload.data() + n_offset, &n_count_network, 4);
    n_offset += 4;

    // Write each peer
    for (const auto& peer : m_vec_peers) {
        // Write address length (2 bytes, network byte order)
        uint16_t n_addr_len = static_cast<uint16_t>(peer.str_address.size());
        uint16_t n_addr_len_network = htons(n_addr_len);
        std::memcpy(payload.data() + n_offset, &n_addr_len_network, 2);
        n_offset += 2;

        // Write address string
        std::memcpy(payload.data() + n_offset, peer.str_address.data(), peer.str_address.size());
        n_offset += peer.str_address.size();

        // Write port (2 bytes, network byte order)
        uint16_t n_port_network = htons(peer.n_port);
        std::memcpy(payload.data() + n_offset, &n_port_network, 2);
        n_offset += 2;
    }

    return payload;
}

/**
 * @brief Deserialize binary payload to peers
 */
bool CPeersMessage::DeserializePayload(const std::vector<uint8_t>& payload) {
    m_vec_peers.clear();

    // Need at least 4 bytes for count
    if (payload.size() < 4) {
        return false;
    }

    size_t n_offset = 0;

    // Read count (4 bytes, network byte order)
    uint32_t n_count_network;
    std::memcpy(&n_count_network, payload.data() + n_offset, 4);
    uint32_t n_count = ntohl(n_count_network);
    n_offset += 4;

    // Read each peer
    for (uint32_t i = 0; i < n_count; i++) {
        // Check if we have enough data for address length
        if (payload.size() < n_offset + 2) {
            return false;
        }

        // Read address length (2 bytes, network byte order)
        uint16_t n_addr_len_network;
        std::memcpy(&n_addr_len_network, payload.data() + n_offset, 2);
        uint16_t n_addr_len = ntohs(n_addr_len_network);
        n_offset += 2;

        // Check if we have enough data for address string and port
        if (payload.size() < n_offset + n_addr_len + 2) {
            return false;
        }

        // Read address string
        std::string str_address(reinterpret_cast<const char*>(payload.data() + n_offset), n_addr_len);
        n_offset += n_addr_len;

        // Read port (2 bytes, network byte order)
        uint16_t n_port_network;
        std::memcpy(&n_port_network, payload.data() + n_offset, 2);
        uint16_t n_port = ntohs(n_port_network);
        n_offset += 2;

        // Validation: Check address is non-empty and port is non-zero
        if (str_address.empty() || n_port == 0) {
            return false;
        }

        // Add peer
        m_vec_peers.emplace_back(str_address, n_port);
    }

    return true;
}

/**
 * @brief Create a copy of this message
 */
std::unique_ptr<CPeerMessage> CPeersMessage::Clone() const {
    return std::make_unique<CPeersMessage>(m_vec_peers, GetMagic());
}

/**
 * @brief Get all peers
 */
const std::vector<CPeerInfo>& CPeersMessage::GetPeers() const {
    return m_vec_peers;
}

/**
 * @brief Add a peer
 */
void CPeersMessage::AddPeer(const std::string& str_address, uint16_t n_port) {
    m_vec_peers.emplace_back(str_address, n_port);
}

/**
 * @brief Get number of peers
 */
size_t CPeersMessage::GetPeerCount() const {
    return m_vec_peers.size();
}

/**
 * @brief Clear all peers
 */
void CPeersMessage::Clear() {
    m_vec_peers.clear();
}
