// ============= peer_node.h =============
#ifndef PEER_NODE_H
#define PEER_NODE_H

#include <string>
#include <memory>

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
 * @class CPeerNode
 * @brief Concrete implementation of peer node information
 *
 * Wraps peer address and port information in a convenient class.
 * Implements IPeerNode interface for future extensibility.
 *
 * Features:
 * - Immutable peer information (const members)
 * - Value semantics (copyable and movable)
 * - Validation of peer information
 * - Convenient identifier generation
 *
 * Example usage:
 *   CPeerNode peer("192.168.1.100", 1984);
 *   std::string addr = peer.GetAddress();      // "192.168.1.100"
 *   int port = peer.GetPort();                 // 1984
 *   std::string id = peer.GetIdentifier();     // "192.168.1.100:1984"
 *   bool valid = peer.IsValid();               // true
 */
class CPeerNode : public IPeerNode {
private:
    std::string str_address;      ///< Peer IP address or hostname
    int n_port;                   ///< Peer listening port
    int64_t n_connection_time;    ///< UNIX UTC timestamp when addpeer request is first sent
    double d_ping_roundtrip_time; ///< Ping round-trip time in milliseconds

public:
    /**
     * @brief Default constructor - creates invalid peer
     */
    CPeerNode();

    /**
     * @brief Construct peer node with address and port
     * @param str_addr Peer IP address or hostname
     * @param n_port_num Peer listening port
     */
    CPeerNode(const std::string& str_addr, int n_port_num);

    /**
     * @brief Virtual destructor
     */
    virtual ~CPeerNode() override = default;

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
};

#endif // PEER_NODE_H
