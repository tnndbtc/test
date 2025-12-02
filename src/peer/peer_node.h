// ============= peer_node.h =============
#ifndef PEER_NODE_H
#define PEER_NODE_H

#include <string>
#include <memory>
#include <cstdint>
#include <mutex>
#include <chrono>
#include <boost/asio.hpp>

/**
 * @interface IPeerNode
 * @brief Interface for peer node information
 *
 * Defines the contract for accessing peer node information.
 * This interface allows for future enhancements and alternative implementations
 * while maintaining a consistent API for peer information access.
 *
 * Future enhancements could include:
 * - Peer reputation tracking
 * - Connection statistics
 * - Peer capabilities/features
 * - Last seen timestamp
 * - Peer version information
 */
class IPeerNode {
public:
    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~IPeerNode() = default;

    /**
     * @brief Get peer IP address or hostname
     * @return Peer address as string
     */
    virtual std::string GetAddress() const = 0;

    /**
     * @brief Get peer listening port
     * @return Port number
     */
    virtual int GetPort() const = 0;

    /**
     * @brief Get peer identifier (address:port format)
     * @return String in format "address:port"
     */
    virtual std::string GetIdentifier() const = 0;

    /**
     * @brief Check if peer information is valid
     * @return true if address is not empty and port is valid, false otherwise
     */
    virtual bool IsValid() const = 0;

    /**
     * @brief Get peer information as JSON string
     * @return JSON string with peer info (address, port, identifier)
     */
    virtual std::string GetInfo() const = 0;
};

/**
 * @enum HandshakeState
 * @brief Bitmap flags for tracking VERSION/VERACK handshake progression
 *
 * Uses independent bits to handle async handshake message ordering.
 * Messages can arrive in any order, and handshake completes when all required bits are set.
 *
 * Bitmap layout:
 * - Bit 0 (0x01): VERSION_SENT - We sent VERSION (outbound peer only)
 * - Bit 1 (0x02): VERSION_RCVD - Received VERSION from peer
 * - Bit 2 (0x04): VERACK_RCVD - Received VERACK from peer
 * - COMPLETE (0x07): All three flags set = handshake complete
 *
 * Example: If VERACK arrives before VERSION:
 *   1. Initial: NONE (0x00)
 *   2. VERACK arrives: 0x04
 *   3. VERSION arrives: 0x04 | 0x02 = 0x06
 *   4. Check: 0x06 != 0x07, still waiting for VERSION_SENT flag
 */
enum HandshakeState : uint8_t {
    NONE = 0,
    VERSION_SENT = (1 << 0),  // 0x01
    VERSION_RCVD = (1 << 1),  // 0x02
    VERACK_RCVD  = (1 << 2),  // 0x04
    COMPLETE = (VERSION_SENT | VERSION_RCVD | VERACK_RCVD)  // 0x07
};

/**
 * @class CPeerNode
 * @brief Unified peer representation with socket, connection state, and metadata
 *
 * Consolidates all peer-related functionality (previously split between CPeerConnection and CPeerNode).
 * Owns the TCP socket, manages connection state, and stores peer metadata.
 * Implements IPeerNode interface for future extensibility.
 *
 * Features:
 * - Owns TCP socket (shared_ptr for async operation lifetime)
 * - Manages connection state (connected, PING tracking)
 * - Move-only semantics (non-copyable to prevent socket duplication)
 * - Thread-safe access to all members via mutex
 * - Always used via shared_ptr<CPeerNode>
 *
 * Example usage:
 *   auto p_peer = std::make_shared<CPeerNode>("192.168.1.100", 1984, socket);
 *   std::string addr = p_peer->GetAddress();      // "192.168.1.100"
 *   auto socket = p_peer->GetSocket();            // Access socket for I/O
 *   bool connected = p_peer->IsConnected();       // Check connection status
 */
class CPeerNode : public IPeerNode {
private:
    mutable std::mutex cs_peer_node;  ///< Mutex for thread-safe access to peer node data

    // Network connection (socket ownership)
    std::shared_ptr<boost::asio::ip::tcp::socket> p_socket;  ///< TCP socket for this peer connection

    // Connection state
    bool f_connected;                 ///< Whether peer is currently connected
    uint8_t m_handshake_state;        ///< VERSION/VERACK handshake progression bitmap (HandshakeState flags)

    // PING/PONG tracking
    uint32_t n_last_ping_nonce;       ///< Nonce of last sent PING message for round-trip tracking
    std::chrono::steady_clock::time_point m_last_ping_send_time;  ///< Timestamp when last PING was sent

    // Peer metadata
    std::string str_address;          ///< Peer IP address or hostname
    int n_port;                       ///< Peer listening port
    int64_t n_connection_time;        ///< UNIX UTC timestamp when connection was established
    double d_ping_roundtrip_time;     ///< Ping round-trip time in milliseconds
    bool f_inbound;                   ///< true if this is an inbound connection, false if outbound
    int32_t n_protocol_version;       ///< Protocol version learned through VERSION/VERACK handshakes
    uint64_t n_services;              ///< 64-bit bitmap of services, learned through VERSION/VERACK handshakes

public:
    /**
     * @brief Construct peer node with address, port, and socket
     * @param str_addr Peer IP address or hostname
     * @param n_port_num Peer listening port
     * @param socket Shared pointer to TCP socket for this connection
     */
    CPeerNode(const std::string& str_addr, int n_port_num,
              std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    /**
     * @brief Copy constructor - DELETED (move-only semantics)
     *
     * CPeerNode is move-only to prevent socket duplication.
     * Use shared_ptr<CPeerNode> for sharing peer objects.
     */
    CPeerNode(const CPeerNode& other) = delete;

    /**
     * @brief Copy assignment operator - DELETED (move-only semantics)
     *
     * CPeerNode is move-only to prevent socket duplication.
     * Use shared_ptr<CPeerNode> for sharing peer objects.
     */
    CPeerNode& operator=(const CPeerNode& other) = delete;

    /**
     * Not needed for now
     * @brief Move constructor - transfers ownership of socket and all members
     * @param other Peer node to move from (leaves other in valid but empty state)
     */
    // CPeerNode(CPeerNode&& other) noexcept;

    /**
     * Not needed for now
     * @brief Move assignment operator - transfers ownership of socket and all members
     * @param other Peer node to move from (leaves other in valid but empty state)
     * @return Reference to this object
     */
    // CPeerNode& operator=(CPeerNode&& other) noexcept;

    /**
     * @brief Destructor - closes socket if still open
     *
     * Performs graceful shutdown of TCP socket (shutdown + close).
     * Ignores errors during shutdown as connection may already be closed.
     */
    virtual ~CPeerNode() override;

    /**
     * @brief Get peer IP address or hostname
     * @return Peer address as string
     */
    virtual std::string GetAddress() const override;

    /**
     * @brief Get peer listening port
     * @return Port number
     */
    virtual int GetPort() const override;

    /**
     * @brief Get peer identifier in "address:port" format
     * @return String in format "address:port"
     */
    virtual std::string GetIdentifier() const override;

    /**
     * @brief Check if peer information is valid
     * @return true if address is not empty and port is in valid range (1-65535)
     */
    virtual bool IsValid() const override;

    /**
     * @brief Equality comparison operator
     * @param other Peer node to compare with
     * @return true if address and port match
     */
    bool operator==(const CPeerNode& other) const;

    /**
     * @brief Inequality comparison operator
     * @param other Peer node to compare with
     * @return true if address or port differ
     */
    bool operator!=(const CPeerNode& other) const;

    /**
     * @brief Less-than comparison operator for use in sorted containers
     * @param other Peer node to compare with
     * @return true if this peer is "less than" other (by address, then port)
     */
    bool operator<(const CPeerNode& other) const;

    /**
     * @brief Get peer information as JSON string
     * @return JSON string with peer info (address, port, identifier)
     *
     * Returns a JSON object containing:
     * - "address": Peer IP address or hostname
     * - "port": Peer listening port
     * - "identifier": Combined "address:port" string
     * - "connection_time": UNIX UTC timestamp when connection was established
     * - "ping_roundtrip_time": Ping round-trip time in milliseconds
     *
     * Example output:
     * {
     *   "address": "192.168.1.100",
     *   "port": 1984,
     *   "identifier": "192.168.1.100:1984",
     *   "connection_time": 1609459200,
     *   "ping_roundtrip_time": 12.5
     * }
     */
    virtual std::string GetInfo() const override;

    /**
     * @brief Get connection time
     * @return UNIX UTC timestamp when addpeer request was first sent
     */
    int64_t GetConnectionTime() const;

    /**
     * @brief Set connection time
     * @param n_time UNIX UTC timestamp
     */
    void SetConnectionTime(int64_t n_time);

    /**
     * @brief Get ping round-trip time
     * @return Round-trip time in milliseconds
     */
    double GetPingRoundtripTime() const;

    /**
     * @brief Set ping round-trip time
     * @param d_time Round-trip time in milliseconds
     */
    void SetPingRoundtripTime(double d_time);

    /**
     * @brief Check if this is an inbound peer
     * @return true if peer connected to us (inbound), false if we connected to peer (outbound)
     */
    bool IsInbound() const;

    /**
     * @brief Set whether this is an inbound peer
     * @param f_is_inbound true for inbound, false for outbound
     */
    void SetInbound(bool f_is_inbound);

    /**
     * @brief Get protocol version
     * @return Protocol version number learned through VERSION/VERACK handshakes
     */
    int32_t GetProtocolVersion() const;

    /**
     * @brief Set protocol version
     * @param n_version Protocol version number
     */
    void SetProtocolVersion(int32_t n_version);

    /**
     * @brief Get services bitmap
     * @return 64-bit services bitmap learned through VERSION/VERACK handshakes
     */
    uint64_t GetServices() const;

    /**
     * @brief Set services bitmap
     * @param n_service_flags 64-bit services bitmap
     */
    void SetServices(uint64_t n_service_flags);

    /**
     * @brief Get the TCP socket for this peer connection
     * @return Shared pointer to socket (may be nullptr if no connection)
     */
    std::shared_ptr<boost::asio::ip::tcp::socket> GetSocket() const;

    /**
     * @brief Check if peer is currently connected
     * @return true if connected, false otherwise
     */
    bool IsConnected() const;

    /**
     * @brief Set connection status
     * @param f_is_connected true if connected, false otherwise
     */
    void SetConnected(bool f_is_connected);

    /**
     * @brief Add handshake state flag(s) to current bitmap
     * @param flag HandshakeState flag(s) to add (bitwise OR)
     */
    bool AddHandshakeFlag(uint8_t flag);

    /**
     * @brief Check if VERSION/VERACK handshake is complete
     * @return true if handshake complete (state == COMPLETE), false otherwise
     */
    bool IsHandshakeComplete() const;

    /**
     * @brief Get the nonce of the last sent PING message
     * @return PING nonce value
     */
    uint32_t GetLastPingNonce() const;

    /**
     * @brief Set the nonce of the last sent PING message
     * @param n_nonce PING nonce value
     */
    void SetLastPingNonce(uint32_t n_nonce);

    /**
     * @brief Get the timestamp when last PING was sent
     * @return Timestamp of last PING
     */
    std::chrono::steady_clock::time_point GetLastPingSendTime() const;

    /**
     * @brief Set the timestamp when last PING was sent
     * @param time Timestamp of last PING
     */
    void SetLastPingSendTime(std::chrono::steady_clock::time_point time);
};

#endif // PEER_NODE_H
