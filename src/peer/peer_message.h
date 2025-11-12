// ============= peer_message.h =============
#ifndef PEER_MESSAGE_H
#define PEER_MESSAGE_H

#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>

// Platform-specific headers for network byte order functions (ntohs, htons)
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>  // For ntohs, htons on Windows (must come before windows.h)
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>  // For ntohs, htons on Linux/POSIX
#endif

#include "blockcore/object_type.h"

/**
 * @brief Read ObjectType::Type from wire format (2 bytes, network byte order)
 * @param p_data Pointer to 2-byte buffer containing object type
 * @return ObjectType::Type value
 *
 * New inventory format uses raw ObjectType::Type values (uint16) instead of characters.
 * Format: 2 bytes in network byte order (big-endian)
 */
inline ObjectType::Type ReadObjectType(const char* p_data) {
    uint16_t n_type;
    std::memcpy(&n_type, p_data, 2);
    return ntohs(n_type);  // Convert from network byte order
}

/**
 * @brief Write ObjectType::Type to wire format (2 bytes, network byte order)
 * @param type Object type to write
 * @param p_buffer Pointer to buffer (must have at least 2 bytes)
 *
 * Converts ObjectType::Type to 2-byte network byte order representation.
 */
inline void WriteObjectType(ObjectType::Type type, char* p_buffer) {
    uint16_t n_type = htons(type);  // Convert to network byte order
    std::memcpy(p_buffer, &n_type, 2);
}

/**
 * @namespace MessageType
 * @brief Types of messages that can be exchanged between peers
 *
 * Defines the protocol message types for peer-to-peer communication.
 * Each message type has a specific purpose and payload format.
 * Message types are string-based for human readability and protocol flexibility.
 */
namespace MessageType {
    const std::string PING = "ping";               ///< Ping message - keep-alive heartbeat
    const std::string PONG = "pong";               ///< Pong response to ping
    const std::string GET_PEERS = "get_peers";     ///< Request list of known peers
    const std::string PEERS = "peers";             ///< Response with peer list
    const std::string TX = "tx";                   ///< Response with transaction data
    const std::string BLOCK = "block";             ///< Response with block data
    const std::string GET_CHAIN = "get_chain";     ///< Request blockchain info
    const std::string CHAIN_INFO = "chain_info";   ///< Blockchain info response
    const std::string INVENTORY = "inv";           ///< Inventory - broadcast hashes of transactions/blocks
    const std::string VERSION = "version";         ///< Version handshake message
    const std::string GETDATA = "getdata";         ///< Request full transaction/block by hash
    const std::string UNKNOWN = "unknown";         ///< Unknown/invalid message type

    /**
     * @brief Check if a message type string is valid
     * @param str_type Message type string
     * @return true if valid message type, false otherwise
     */
    bool IsValid(const std::string& str_type);
}

/**
 * @class CPeerMessage
 * @brief Message protocol class for peer-to-peer communication
 *
 * Encapsulates messages exchanged between peers in the P2P network.
 * Provides serialization/deserialization for network transmission.
 *
 * Message format:
 * - 1 byte: Message type length (uint8_t)
 * - N bytes: Message type string (e.g., "ping", "get_peers")
 * - 4 bytes: Payload length (uint32_t, network byte order)
 * - M bytes: Payload data
 *
 * Example usage:
 *   // Create and serialize a PING message
 *   CPeerMessage ping_msg(MessageType::PING);
 *   std::string serialized = ping_msg.Serialize();
 *
 *   // Deserialize received message
 *   CPeerMessage received;
 *   if (received.Deserialize(data)) {
 *       if (received.GetType() == MessageType::PING) {
 *           // Handle ping...
 *       }
 *   }
 */
class CPeerMessage {
private:
    std::string m_str_type;         ///< Message type string
    std::vector<uint8_t> m_payload; ///< Message payload data

    /**
     * @brief Convert uint32_t to network byte order (big-endian)
     * @param n_value Value to convert
     * @return Value in network byte order
     */
    static uint32_t HostToNetwork(uint32_t n_value);

    /**
     * @brief Convert uint32_t from network byte order to host byte order
     * @param n_value Value to convert
     * @return Value in host byte order
     */
    static uint32_t NetworkToHost(uint32_t n_value);

public:
    /**
     * @brief Default constructor - creates UNKNOWN message
     */
    CPeerMessage();

    /**
     * @brief Construct message with specific type
     * @param str_type Message type string (e.g., MessageType::PING)
     */
    explicit CPeerMessage(const std::string& str_type);

    /**
     * @brief Construct message with type and payload
     * @param str_type Message type string
     * @param str_payload Payload string
     */
    CPeerMessage(const std::string& str_type, const std::string& str_payload);

    /**
     * @brief Construct message with type and binary payload
     * @param str_type Message type string
     * @param payload Payload bytes
     */
    CPeerMessage(const std::string& str_type, const std::vector<uint8_t>& payload);

    /**
     * @brief Get message type
     * @return Message type string
     */
    const std::string& GetType() const;

    /**
     * @brief Set message type
     * @param str_type New message type string
     */
    void SetType(const std::string& str_type);

    /**
     * @brief Get payload as string
     * @return Payload as string
     */
    std::string GetPayloadString() const;

    /**
     * @brief Get payload as byte vector
     * @return Payload bytes
     */
    const std::vector<uint8_t>& GetPayloadBytes() const;

    /**
     * @brief Set payload from string
     * @param str_payload Payload string
     */
    void SetPayload(const std::string& str_payload);

    /**
     * @brief Set payload from byte vector
     * @param payload Payload bytes
     */
    void SetPayload(const std::vector<uint8_t>& payload);

    /**
     * @brief Get payload size in bytes
     * @return Payload size
     */
    size_t GetPayloadSize() const;

    /**
     * @brief Serialize message to byte string for transmission
     * @return Serialized message (type_length + type + payload_length + payload)
     *
     * Format: [1 byte type_length][N bytes type][4 bytes payload_length][M bytes payload]
     */
    std::string Serialize() const;

    /**
     * @brief Deserialize message from byte string
     * @param str_data Serialized message data
     * @return true if deserialization successful, false otherwise
     *
     * Parses message format: [1 byte type_length][N bytes type][4 bytes payload_length][M bytes payload]
     * Returns false if data is too short or length is invalid.
     */
    bool Deserialize(const std::string& str_data);

    /**
     * @brief Check if message is valid
     * @return true if message has valid type and payload
     */
    bool IsValid() const;

    /**
     * @brief Get string representation of this message's type
     * @return String name of message type (same as GetType())
     */
    std::string GetTypeString() const;

    /**
     * @brief Get minimum serialized message size (header only with smallest type)
     * @return Minimum size in bytes (varies based on type string length)
     */
    static constexpr size_t GetMinHeaderSize() { return 5; }  // 1 byte type_length + 0 bytes type + 4 bytes payload_length
};

#endif // PEER_MESSAGE_H
