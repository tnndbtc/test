// ============= version_message.h =============
#ifndef VERSION_MESSAGE_H
#define VERSION_MESSAGE_H

#include "peer/peer_message.h"
#include <cstdint>
#include <memory>
#include <string>
#include <array>

/**
 * @struct CNetworkAddress
 * @brief Network address structure for VERSION message
 *
 * Contains services bitmap, IPv6 address, and port.
 * IPv4 addresses are encoded as IPv4-mapped IPv6 addresses.
 */
struct CNetworkAddress {
    uint64_t n_services;               ///< 64-bit services bitmap
    std::array<uint8_t, 16> address;   ///< 16 bytes IPv6 address (IPv4-mapped for IPv4)
    uint16_t n_port;                   ///< 2 bytes port number

    CNetworkAddress();
    CNetworkAddress(uint64_t services, const std::string& str_ip, uint16_t port);
};

/**
 * @class CVersionMessage
 * @brief Type-safe VERSION message implementation
 *
 * VERSION messages exchange protocol version and client information during handshake.
 * Extended to include protocol version, timestamps, network addresses, nonce, and chain height.
 *
 * Payload format:
 * - protocol_version: 4 bytes (int32_t)
 * - timestamp: 8 bytes (int64_t, UNIX time)
 * - address_recv: 26 bytes (CNetworkAddress - peer to add)
 * - address_from: 26 bytes (CNetworkAddress - sender)
 * - nonce: 8 bytes (uint64_t)
 * - chain_tip_height: 4 bytes (int32_t)
 *
 * Usage:
 *   CVersionMessage version(LOCALNET_MAGIC);
 *   version.SetProtocolVersion(PROTOCOL_VERSION);
 *   std::string serialized = version.Serialize();
 */
class CVersionMessage : public CPeerMessage {
private:
    int32_t m_n_protocol_version;        ///< Protocol version (from src/blockcore/protocol.h)
    int64_t m_n_timestamp;               ///< UNIX timestamp
    CNetworkAddress m_address_recv;      ///< Address of peer to add (services=0 on first VERSION)
    CNetworkAddress m_address_from;      ///< Address of sender (services from protocol.h)
    uint64_t m_n_nonce;                  ///< Random nonce for connection uniqueness
    int32_t m_n_chain_tip_height;        ///< Current blockchain tip height

public:
    /**
     * @brief Constructor with magic
     */
    CVersionMessage(uint32_t n_magic);

    /**
     * @brief Get message type (always returns "version")
     */
    std::string GetType() const override;

    /**
     * @brief Serialize VERSION message to binary payload
     */
    std::vector<uint8_t> SerializePayload() const override;

    /**
     * @brief Deserialize binary payload to VERSION message
     */
    bool DeserializePayload(const std::vector<uint8_t>& payload) override;

    /**
     * @brief Create a copy of this message
     */
    std::unique_ptr<CPeerMessage> Clone() const override;

    /**
     * @brief Validate message (always true for version)
     */

    /**
     * @brief Get protocol version
     */
    int32_t GetProtocolVersion() const;

    /**
     * @brief Get timestamp
     */
    int64_t GetTimestamp() const;

    /**
     * @brief Get receiver address
     */
    const CNetworkAddress& GetAddressRecv() const;

    /**
     * @brief Set receiver address
     */
    void SetAddressRecv(const CNetworkAddress& address);

    /**
     * @brief Get sender address
     */
    const CNetworkAddress& GetAddressFrom() const;

    /**
     * @brief Set sender address
     */
    void SetAddressFrom(const CNetworkAddress& address);

    /**
     * @brief Get nonce
     */
    uint64_t GetNonce() const;

    /**
     * @brief Get chain tip height
     */
    int32_t GetChainTipHeight() const;

    /**
     * @brief Set chain tip height
     */
    void SetChainTipHeight(int32_t n_height);
};

#endif // VERSION_MESSAGE_H
