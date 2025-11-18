// ============= peers_message.h =============
#ifndef PEERS_MESSAGE_H
#define PEERS_MESSAGE_H

#include "peer/peer_message.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * @struct CPeerInfo
 * @brief Peer address information
 */
struct CPeerInfo {
    std::string str_address;  ///< IP address or hostname
    uint16_t n_port;          ///< Port number

    CPeerInfo(const std::string& address, uint16_t port)
        : str_address(address), n_port(port) {}
};

/**
 * @class CPeersMessage
 * @brief Type-safe PEERS message implementation
 *
 * PEERS messages contain a list of known peer addresses.
 * Sent in response to GET_PEERS requests.
 *
 * Payload format: [count:4bytes][address_length:2bytes][address:N bytes][port:2bytes]...
 *
 * Usage:
 *   CPeersMessage peers(LOCALNET_MAGIC);
 *   peers.AddPeer("192.168.1.1", 8333);
 *   std::string serialized = peers.Serialize();
 */
class CPeersMessage : public CPeerMessage {
private:
    std::vector<CPeerInfo> m_vec_peers;  ///< List of peer addresses

public:
    /**
     * @brief Constructor with magic
     */
    explicit CPeersMessage(uint32_t n_magic);

    /**
     * @brief Constructor with peers and magic
     */
    CPeersMessage(const std::vector<CPeerInfo>& vec_peers, uint32_t n_magic);

    /**
     * @brief Get message type (always returns "peers")
     */
    std::string GetType() const override;

    /**
     * @brief Serialize peers to binary payload
     */
    std::vector<uint8_t> SerializePayload() const override;

    /**
     * @brief Deserialize binary payload to peers
     */
    bool DeserializePayload(const std::vector<uint8_t>& payload) override;

    /**
     * @brief Create a copy of this message
     */
    std::unique_ptr<CPeerMessage> Clone() const override;

    /**
     * @brief Validate message
     */

    /**
     * @brief Get all peers
     */
    const std::vector<CPeerInfo>& GetPeers() const;

    /**
     * @brief Add a peer
     */
    void AddPeer(const std::string& str_address, uint16_t n_port);

    /**
     * @brief Get number of peers
     */
    size_t GetPeerCount() const;

    /**
     * @brief Clear all peers
     */
    void Clear();
};

#endif // PEERS_MESSAGE_H
