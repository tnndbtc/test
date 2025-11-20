// ============= peer_message.h =============
#ifndef PEER_MESSAGE_H
#define PEER_MESSAGE_H

#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>
#include <memory>

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
    const std::string VERACK = "verack";           ///< Verack handshake message
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
 * @brief Abstract base class for peer-to-peer communication messages
 *
 * This is an abstract base class that defines the interface for all P2P messages.
 * Derived classes implement specific message types (PING, PONG, INVENTORY, etc.)
 * with type-safe payload handling.
 *
 * Message format (wire protocol):
 * - 4 bytes: Network magic bytes (uint32_t, network byte order)
 * - 1 byte: Message type length (uint8_t)
 * - N bytes: Message type string (e.g., "ping", "get_peers")
 * - 4 bytes: Payload length (uint32_t, network byte order)
 * - M bytes: Payload data (format defined by derived class)
 *
 * Derived classes must implement:
 * - GetType(): Return message type string
 * - SerializePayload(): Convert message data to bytes
 * - DeserializePayload(): Parse bytes into message data
 * - Clone(): Create a copy of the message
 *
 * Example usage:
 *   // Create a PING message
 *   auto ping_msg = std::make_unique<CPingMessage>(nonce, magic);
 *   std::string serialized = ping_msg->Serialize();
 *
 *   // Deserialize received message
 *   auto received = CPeerMessageFactory::CreateFromWireData(data, magic);
 *   if (received && received->GetType() == MessageType::PING) {
 *       // Handle ping...
 *   }
 */
class CPeerMessage {
protected:
    uint32_t m_n_magic;             ///< Network magic bytes for protocol validation

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
     * @brief Constructor with network magic
     * @param n_magic Network magic bytes
     */
    explicit CPeerMessage(uint32_t n_magic = 0);

    /**
     * @brief Virtual destructor for proper cleanup of derived classes
     */
    virtual ~CPeerMessage() = default;

    // ========== Pure Virtual Methods (must be implemented by derived classes) ==========

    /**
     * @brief Get message type string
     * @return Message type (e.g., "ping", "inv", "getdata")
     */
    virtual std::string GetType() const = 0;

    /**
     * @brief Serialize message-specific payload to bytes
     * @return Payload bytes
     *
     * Derived classes implement this to convert their specific data fields
     * into the binary format for network transmission.
     */
    virtual std::vector<uint8_t> SerializePayload() const = 0;

    /**
     * @brief Deserialize message-specific payload from bytes
     * @param payload Payload bytes
     * @return true if successfully parsed, false otherwise
     *
     * Derived classes implement this to parse their specific data fields
     * from the binary format received from network.
     */
    virtual bool DeserializePayload(const std::vector<uint8_t>& payload) = 0;

    /**
     * @brief Create a copy of this message
     * @return Unique pointer to cloned message
     *
     * Derived classes implement this to support polymorphic copying.
     */
    virtual std::unique_ptr<CPeerMessage> Clone() const = 0;

    // ========== Non-Virtual Methods (wire format handling) ==========

    /**
     * @brief Get network magic bytes
     * @return Network magic value
     */
    uint32_t GetMagic() const;

    /**
     * @brief Set network magic bytes
     * @param n_magic Network magic value
     */
    void SetMagic(uint32_t n_magic);

    /**
     * @brief Serialize complete message to wire format
     * @return Serialized message bytes
     *
     * Format: [4 bytes magic][1 byte type_length][N bytes type][4 bytes payload_length][M bytes payload]
     * This method is NOT virtual - it handles the wire protocol and calls SerializePayload()
     */
    std::string Serialize() const;

    /**
     * @brief Deserialize complete message from wire format
     * @param str_data Serialized message data
     * @param n_expected_magic Expected network magic bytes (0 = don't validate)
     * @return true if deserialization successful and magic matches, false otherwise
     *
     * This method is NOT virtual - it handles the wire protocol and calls DeserializePayload()
     */
    bool Deserialize(const std::string& str_data, uint32_t n_expected_magic = 0);

    /**
     * @brief Get minimum serialized message size (header only)
     * @return Minimum size in bytes
     */
    static constexpr size_t GetMinHeaderSize() { return 9; }  // 4 bytes magic + 1 byte type_length + 0 bytes type + 4 bytes payload_length
};

#endif // PEER_MESSAGE_H
