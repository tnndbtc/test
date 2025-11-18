// ============= peer_message_factory.h =============
#ifndef PEER_MESSAGE_FACTORY_H
#define PEER_MESSAGE_FACTORY_H

#include "peer_message.h"
#include <string>
#include <memory>

/**
 * @class CPeerMessageFactory
 * @brief Factory for creating P2P message objects from wire data or message type
 *
 * This factory provides two main creation methods:
 * 1. CreateFromWireData(): Parse complete serialized message and return appropriate derived class
 * 2. CreateFromType(): Create empty message of specified type (for sending)
 *
 * The factory automatically selects the correct derived class based on message type:
 * - MessageType::PING → CPingMessage
 * - MessageType::PONG → CPongMessage
 * - MessageType::INVENTORY → CInventoryMessage
 * - etc.
 *
 * Usage for receiving:
 *   std::string wire_data = receive_from_network();
 *   auto msg = CPeerMessageFactory::CreateFromWireData(wire_data, LOCALNET_MAGIC);
 *   if (msg && msg->GetType() == MessageType::PING) {
 *       auto ping = dynamic_cast<CPingMessage*>(msg.get());
 *       // handle ping...
 *   }
 *
 * Usage for sending:
 *   auto ping = CPeerMessageFactory::CreatePingMessage(nonce, magic);
 *   std::string serialized = ping->Serialize();
 *   send_to_network(serialized);
 */
class CPeerMessageFactory {
public:
    /**
     * @brief Create message object from serialized wire data
     * @param str_wire_data Complete serialized message including header
     * @param n_expected_magic Expected network magic bytes (0 = don't validate)
     * @return Unique pointer to appropriate derived message class, or nullptr on failure
     *
     * This method:
     * 1. Parses the wire format header to extract message type
     * 2. Creates appropriate derived class instance
     * 3. Calls Deserialize() to parse complete message
     * 4. Returns nullptr if magic mismatch or parse failure
     *
     * Example:
     *   auto msg = CreateFromWireData(received_data, LOCALNET_MAGIC);
     *   if (msg) {
     *       std::cout << "Received: " << msg->GetType() << std::endl;
     *   }
     */
    static std::unique_ptr<CPeerMessage> CreateFromWireData(
        const std::string& str_wire_data,
        uint32_t n_expected_magic = 0
    );

    /**
     * @brief Create empty message of specified type (for sending)
     * @param str_type Message type string (e.g., "ping", "inv")
     * @param n_magic Network magic bytes
     * @return Unique pointer to appropriate derived message class
     *
     * Creates an empty message that you can then populate with data.
     */
    /*
    static std::unique_ptr<CPeerMessage> CreateFromType(
        const std::string& str_type,
        uint32_t n_magic
    );
    */

private:
    /**
     * @brief Parse message type from wire data header (without deserializing full message)
     * @param str_wire_data Wire data
     * @param[out] str_type Parsed message type
     * @return true if successfully parsed type, false otherwise
     *
     * Internal helper that extracts just the message type field from wire format:
     * [4 bytes magic][1 byte type_length][N bytes type][...]
     */
    static bool ExtractMessageType(const std::string& str_wire_data, std::string& str_type);
};

#endif // PEER_MESSAGE_FACTORY_H
