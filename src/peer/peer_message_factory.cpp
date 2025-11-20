// ============= peer_message_factory.cpp =============
#include "peer_message_factory.h"
#include "messages/ping_message.h"
#include "messages/pong_message.h"
#include "messages/inventory_message.h"
#include "messages/getdata_message.h"
#include "messages/version_message.h"
#include "messages/tx_message.h"
#include "messages/block_message.h"
#include "messages/getpeers_message.h"
#include "messages/peers_message.h"
#include "messages/getchain_message.h"
#include "messages/chaininfo_message.h"
#include "messages/unknown_message.h"
#include "logger/logger.h"
#include <cstring>

/**
 * @brief Parse message type from wire data header
 */
bool CPeerMessageFactory::ExtractMessageType(const std::string& str_wire_data, std::string& str_type) {
    // Wire format: [4 bytes magic][1 byte type_length][N bytes type][4 bytes payload_length][M bytes payload]

    // Need at least 5 bytes: 4 for magic + 1 for type_length
    if (str_wire_data.size() < 5) {
        return false;
    }

    // Skip magic bytes (4 bytes)
    size_t n_offset = 4;

    // Read type length (1 byte)
    uint8_t n_type_length = static_cast<uint8_t>(str_wire_data[n_offset]);
    n_offset += 1;

    // Check if we have enough data for type string
    if (str_wire_data.size() < n_offset + n_type_length) {
        return false;
    }

    // Extract type string
    str_type = str_wire_data.substr(n_offset, n_type_length);
    return true;
}

/**
 * @brief Create message object from serialized wire data
 */
std::unique_ptr<CPeerMessage> CPeerMessageFactory::CreateFromWireData(
    const std::string& str_wire_data,
    uint32_t n_expected_magic
) {
    // Extract message type from wire data
    std::string str_type;
    if (!ExtractMessageType(str_wire_data, str_type)) {
        return nullptr;  // Invalid wire data
    }

    // Create appropriate message class based on type
    std::unique_ptr<CPeerMessage> p_msg;

    // Phase 2, 3, 4 & 6: Use specific message classes
    if (str_type == MessageType::PING) {
        p_msg = std::make_unique<CPingMessage>(n_expected_magic);
    } else if (str_type == MessageType::PONG) {
        p_msg = std::make_unique<CPongMessage>(0, n_expected_magic);
    } else if (str_type == MessageType::INVENTORY) {
        p_msg = std::make_unique<CInventoryMessage>(n_expected_magic);
    } else if (str_type == MessageType::GETDATA) {
        p_msg = std::make_unique<CGetDataMessage>(n_expected_magic);
    } else if (str_type == MessageType::VERSION) {
        p_msg = std::make_unique<CVersionMessage>(n_expected_magic);
    } else if (str_type == MessageType::TX) {
        p_msg = std::make_unique<CTxMessage>(nullptr, n_expected_magic);
    } else if (str_type == MessageType::BLOCK) {
        p_msg = std::make_unique<CBlockMessage>(nullptr, n_expected_magic);
    } else if (str_type == MessageType::GET_PEERS) {
        p_msg = std::make_unique<CGetPeersMessage>(n_expected_magic);
    } else if (str_type == MessageType::PEERS) {
        p_msg = std::make_unique<CPeersMessage>(n_expected_magic);
    } else if (str_type == MessageType::GET_CHAIN) {
        p_msg = std::make_unique<CGetChainMessage>(n_expected_magic);
    } else if (str_type == MessageType::CHAIN_INFO) {
        p_msg = std::make_unique<CChainInfoMessage>(n_expected_magic);
    } else {
        // Unknown message type
        LOG_WARN("Unknown message type: " + str_type);
        return nullptr;
    }

    // Deserialize the complete message
    if (!p_msg->Deserialize(str_wire_data, n_expected_magic)) {
        LOG_WARN("Deserialization failed, invalid data");
        return nullptr;  // Deserialization failed (magic mismatch or invalid data)
    }

    return p_msg;
}

/**
 * @brief Create empty message of specified type
 */
/*
std::unique_ptr<CPeerMessage> CPeerMessageFactory::CreateFromType(
    const std::string& str_type,
    uint32_t n_magic
) {
    // Phase 2, 3, 4 & 6: Use specific message classes
    if (str_type == MessageType::PING) {
        return std::make_unique<CPingMessage>(0, n_magic);
    } else if (str_type == MessageType::PONG) {
        return std::make_unique<CPongMessage>(0, n_magic);
    } else if (str_type == MessageType::INVENTORY) {
        return std::make_unique<CInventoryMessage>(n_magic);
    } else if (str_type == MessageType::GETDATA) {
        return std::make_unique<CGetDataMessage>(n_magic);
    } else if (str_type == MessageType::VERSION) {
        return std::make_unique<CVersionMessage>("", n_magic);
    } else if (str_type == MessageType::TX) {
        return std::make_unique<CTxMessage>("", n_magic);
    } else if (str_type == MessageType::BLOCK) {
        return std::make_unique<CBlockMessage>("", n_magic);
    } else if (str_type == MessageType::GET_PEERS) {
        return std::make_unique<CGetPeersMessage>(n_magic);
    } else if (str_type == MessageType::PEERS) {
        return std::make_unique<CPeersMessage>(n_magic);
    } else if (str_type == MessageType::GET_CHAIN) {
        return std::make_unique<CGetChainMessage>(n_magic);
    } else if (str_type == MessageType::CHAIN_INFO) {
        return std::make_unique<CChainInfoMessage>(n_magic);
    }
    // Unknown message type
    LOG_WARN("Unknown message type: " + str_type);
    // Unknown message type
    return std::make_unique<CUnknownMessage>(n_magic);
}
*/
