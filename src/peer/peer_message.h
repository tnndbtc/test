// ============= peer_message.h =============
#ifndef PEER_MESSAGE_H
#define PEER_MESSAGE_H

#include <string>
#include <vector>
#include <cstdint>

/**
 * @enum EMessageType
 * @brief Types of messages that can be exchanged between peers
 *
 * Defines the protocol message types for peer-to-peer communication.
 * Each message type has a specific purpose and payload format.
 */
enum class EMessageType : uint8_t {
    PING = 0,           ///< Ping message - keep-alive heartbeat
    PONG = 1,           ///< Pong response to ping
    GET_PEERS = 2,      ///< Request list of known peers
    PEERS = 3,          ///< Response with peer list
    TX_IDS = 4,         ///< Broadcast transaction IDs
    GET_TX = 5,         ///< Request transaction by ID
    TX = 6,             ///< Transaction data
    GET_BLOCK = 7,      ///< Request block by hash
    BLOCK = 8,          ///< Block data
    GET_CHAIN = 9,      ///< Request blockchain info
    CHAIN_INFO = 10,    ///< Blockchain info response
    UNKNOWN = 255       ///< Unknown/invalid message type
};

/**
 * @class CPeerMessage
 * @brief Message protocol class for peer-to-peer communication
 *
 * Encapsulates messages exchanged between peers in the P2P network.
 * Provides serialization/deserialization for network transmission.
 *
 * Message format:
 * - 1 byte: Message type (EMessageType)
 * - 4 bytes: Payload length (uint32_t, network byte order)
 * - N bytes: Payload data
 *
 * Example usage:
 *   // Create and serialize a PING message
 *   CPeerMessage ping_msg(EMessageType::PING);
 *   std::string serialized = ping_msg.Serialize();
 *
 *   // Deserialize received message
 *   CPeerMessage received;
 *   if (received.Deserialize(data)) {
 *       if (received.GetType() == EMessageType::PING) {
 *           // Handle ping...
 *       }
 *   }
 */
class CPeerMessage {
private:
    EMessageType m_type;           ///< Message type
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
     * @param type Message type
     */
    explicit CPeerMessage(EMessageType type);

    /**
     * @brief Construct message with type and payload
     * @param type Message type
     * @param str_payload Payload string
     */
    CPeerMessage(EMessageType type, const std::string& str_payload);

    /**
     * @brief Construct message with type and binary payload
     * @param type Message type
     * @param payload Payload bytes
     */
    CPeerMessage(EMessageType type, const std::vector<uint8_t>& payload);

    /**
     * @brief Get message type
     * @return Message type
     */
    EMessageType GetType() const;

    /**
     * @brief Set message type
     * @param type New message type
     */
    void SetType(EMessageType type);

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
     * @return Serialized message (type + length + payload)
     *
     * Format: [1 byte type][4 bytes length][N bytes payload]
     */
    std::string Serialize() const;

    /**
     * @brief Deserialize message from byte string
     * @param str_data Serialized message data
     * @return true if deserialization successful, false otherwise
     *
     * Parses message format: [1 byte type][4 bytes length][N bytes payload]
     * Returns false if data is too short or length is invalid.
     */
    bool Deserialize(const std::string& str_data);

    /**
     * @brief Check if message is valid
     * @return true if message has valid type and payload
     */
    bool IsValid() const;

    /**
     * @brief Get string representation of message type
     * @param type Message type
     * @return String name of message type
     */
    static std::string TypeToString(EMessageType type);

    /**
     * @brief Get string representation of this message's type
     * @return String name of message type
     */
    std::string GetTypeString() const;

    /**
     * @brief Get minimum serialized message size (header only)
     * @return Minimum size in bytes (5 bytes: 1 type + 4 length)
     */
    static constexpr size_t GetHeaderSize() { return 5; }
};

#endif // PEER_MESSAGE_H
