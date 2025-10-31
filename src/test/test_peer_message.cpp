// ============= test_peer_message.cpp =============
#include "unit_test.h"
#include "peer/peer_message.h"
#include <cstring>

using namespace UnitTest;

// Helper function to compare message types
static bool CompareMessageType(const std::string& actual, const std::string& expected) {
    return actual == expected;
}

// Helper function to compare byte vectors
static bool CompareByteVectors(const std::vector<uint8_t>& v1, const std::vector<uint8_t>& v2) {
    if (v1.size() != v2.size()) return false;
    for (size_t i = 0; i < v1.size(); i++) {
        if (v1[i] != v2[i]) return false;
    }
    return true;
}

/**
 * @brief Test CPeerMessage default constructor
 */
TEST(PeerMessage_DefaultConstructor) {
    CPeerMessage msg;

    ASSERT_TRUE(CompareMessageType(msg.GetType(), MessageType::UNKNOWN), "Default type should be UNKNOWN");
    ASSERT_EQUAL(msg.GetPayloadSize(), (size_t)0, "Default payload should be empty");
    ASSERT_FALSE(msg.IsValid(), "Default message should be invalid");
    ASSERT_EQUAL(msg.GetTypeString(), std::string("unknown"), "Default type string should be 'unknown'");
}

/**
 * @brief Test CPeerMessage constructor with type
 */
TEST(PeerMessage_ConstructorWithType) {
    CPeerMessage ping(MessageType::PING);

    ASSERT_TRUE(CompareMessageType(ping.GetType(), MessageType::PING), "Type should be PING");
    ASSERT_EQUAL(ping.GetPayloadSize(), (size_t)0, "Payload should be empty");
    ASSERT_TRUE(ping.IsValid(), "PING message should be valid");
    ASSERT_EQUAL(ping.GetTypeString(), std::string("ping"), "Type string should be 'ping'");
}

/**
 * @brief Test CPeerMessage constructor with type and string payload
 */
TEST(PeerMessage_ConstructorWithStringPayload) {
    std::string payload = "Hello, Peer!";
    CPeerMessage msg(MessageType::TXS, payload);

    ASSERT_TRUE(CompareMessageType(msg.GetType(), MessageType::TXS), "Type should be TXS");
    ASSERT_EQUAL(msg.GetPayloadSize(), payload.size(), "Payload size should match");
    ASSERT_EQUAL(msg.GetPayloadString(), payload, "Payload string should match");
    ASSERT_TRUE(msg.IsValid(), "TXS message should be valid");
}

/**
 * @brief Test CPeerMessage constructor with type and binary payload
 */
TEST(PeerMessage_ConstructorWithBinaryPayload) {
    std::vector<uint8_t> payload = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0xFD};
    CPeerMessage msg(MessageType::BLOCKS, payload);

    ASSERT_TRUE(CompareMessageType(msg.GetType(), MessageType::BLOCKS), "Type should be BLOCKS");
    ASSERT_EQUAL(msg.GetPayloadSize(), payload.size(), "Payload size should match");
    ASSERT_TRUE(CompareByteVectors(msg.GetPayloadBytes(), payload), "Payload bytes should match");
}

/**
 * @brief Test serialization with empty payload
 */
TEST(PeerMessage_SerializeEmpty) {
    CPeerMessage ping(MessageType::PING);
    std::string serialized = ping.Serialize();

    // Format: [1 byte type_length][4 bytes "ping"][4 bytes payload_length]
    // Should be 1 + 4 + 4 = 9 bytes
    ASSERT_EQUAL(serialized.size(), (size_t)9, "Serialized size should be 9 bytes");
    ASSERT_EQUAL((uint8_t)serialized[0], (uint8_t)4, "First byte should be type length (4)");

    // Extract type string
    std::string type_str = serialized.substr(1, 4);
    ASSERT_EQUAL(type_str, std::string("ping"), "Type string should be 'ping'");

    // Payload length should be 0 (4 bytes at offset 5, network byte order)
    uint32_t length_network;
    std::memcpy(&length_network, serialized.data() + 5, 4);
    uint32_t length = ntohl(length_network);
    ASSERT_EQUAL(length, (uint32_t)0, "Payload length field should be 0");
}

/**
 * @brief Test serialization with small payload
 */
TEST(PeerMessage_SerializeSmallPayload) {
    std::string payload = "test";
    CPeerMessage msg(MessageType::PONG, payload);
    std::string serialized = msg.Serialize();

    // Format: [1 byte type_length][4 bytes "pong"][4 bytes payload_length][4 bytes "test"]
    // Should be 1 + 4 + 4 + 4 = 13 bytes
    ASSERT_EQUAL(serialized.size(), (size_t)13, "Serialized size should be 13 bytes");
    ASSERT_EQUAL((uint8_t)serialized[0], (uint8_t)4, "First byte should be type length (4)");

    // Extract type string
    std::string type_str = serialized.substr(1, 4);
    ASSERT_EQUAL(type_str, std::string("pong"), "Type string should be 'pong'");

    // Extract and verify payload (starts at offset 9: 1 + 4 + 4)
    std::string extracted_payload = serialized.substr(9);
    ASSERT_EQUAL(extracted_payload, payload, "Payload should match");
}

/**
 * @brief Test serialization with large payload
 */
TEST(PeerMessage_SerializeLargePayload) {
    std::string large_payload(1000, 'X');
    CPeerMessage msg(MessageType::TXS, large_payload);
    std::string serialized = msg.Serialize();

    // Format: [1 byte type_length][3 bytes "txs"][4 bytes payload_length][1000 bytes payload]
    // Should be 1 + 3 + 4 + 1000 = 1008 bytes
    ASSERT_EQUAL(serialized.size(), (size_t)1008, "Serialized size should be 1008 bytes");
    ASSERT_EQUAL((uint8_t)serialized[0], (uint8_t)3, "First byte should be type length (3)");

    // Extract type string
    std::string type_str = serialized.substr(1, 3);
    ASSERT_EQUAL(type_str, std::string("txs"), "Type string should be 'txs'");

    // Extract and verify payload (starts at offset 8: 1 + 3 + 4)
    std::string extracted_payload = serialized.substr(8);
    ASSERT_EQUAL(extracted_payload, large_payload, "Large payload should match");
}

/**
 * @brief Test deserialization with valid data
 */
TEST(PeerMessage_DeserializeValid) {
    // Create a simple PING message manually
    // Format: [1 byte type_length][4 bytes "ping"][4 bytes payload_length]
    std::string data;
    data.push_back((char)4); // Type length = 4
    data.append("ping");     // Type string

    // Payload length = 0 (4 bytes, network byte order)
    uint32_t length = htonl(0);
    data.append(reinterpret_cast<const char*>(&length), 4);

    CPeerMessage msg;
    bool result = msg.Deserialize(data);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_TRUE(CompareMessageType(msg.GetType(), MessageType::PING), "Type should be PING");
    ASSERT_EQUAL(msg.GetPayloadSize(), (size_t)0, "Payload should be empty");
}

/**
 * @brief Test deserialization with payload
 */
TEST(PeerMessage_DeserializeWithPayload) {
    std::string payload_data = "Hello";

    // Manually construct serialized message
    // Format: [1 byte type_length][4 bytes "pong"][4 bytes payload_length][5 bytes "Hello"]
    std::string data;
    data.push_back((char)4); // Type length = 4
    data.append("pong");     // Type string

    // Payload length = 5 (network byte order)
    uint32_t length = htonl(5);
    data.append(reinterpret_cast<const char*>(&length), 4);
    data.append(payload_data);

    CPeerMessage msg;
    bool result = msg.Deserialize(data);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_TRUE(CompareMessageType(msg.GetType(), MessageType::PONG), "Type should be PONG");
    ASSERT_EQUAL(msg.GetPayloadSize(), (size_t)5, "Payload size should be 5");
    ASSERT_EQUAL(msg.GetPayloadString(), payload_data, "Payload should match");
}

/**
 * @brief Test deserialization with insufficient header data
 */
TEST(PeerMessage_DeserializeTooShort) {
    // Only 3 bytes, but minimum is 1 + type_length + 4
    // If first byte claims type_length=4, we need at least 1+4+4=9 bytes
    std::string data;
    data.push_back((char)4); // Type length = 4
    data.append("AB");       // Only 2 bytes of type, not 4

    CPeerMessage msg;
    bool result = msg.Deserialize(data);

    ASSERT_FALSE(result, "Deserialization should fail with insufficient data");
}

/**
 * @brief Test deserialization with insufficient payload data
 */
TEST(PeerMessage_DeserializeInsufficientPayload) {
    // Format: [1 byte type_length][9 bytes "get_peers"][4 bytes payload_length][payload]
    std::string data;
    data.push_back((char)9);    // Type length = 9
    data.append("get_peers");   // Type string

    // Claim payload length = 100, but only provide 5 bytes
    uint32_t length = htonl(100);
    data.append(reinterpret_cast<const char*>(&length), 4);
    data.append("ABCDE"); // Only 5 bytes, not 100

    CPeerMessage msg;
    bool result = msg.Deserialize(data);

    ASSERT_FALSE(result, "Deserialization should fail with insufficient payload");
}

/**
 * @brief Test round-trip serialization and deserialization
 */
TEST(PeerMessage_RoundTripEmpty) {
    CPeerMessage original(MessageType::GET_PEERS);

    std::string serialized = original.Serialize();

    CPeerMessage deserialized;
    bool result = deserialized.Deserialize(serialized);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_TRUE(CompareMessageType(deserialized.GetType(), original.GetType()), "Type should match");
    ASSERT_EQUAL(deserialized.GetPayloadSize(), original.GetPayloadSize(), "Payload size should match");
}

/**
 * @brief Test round-trip with string payload
 */
TEST(PeerMessage_RoundTripWithPayload) {
    std::string original_payload = "This is a test message!";
    CPeerMessage original(MessageType::TXS, original_payload);

    std::string serialized = original.Serialize();

    CPeerMessage deserialized;
    bool result = deserialized.Deserialize(serialized);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_TRUE(CompareMessageType(deserialized.GetType(), original.GetType()), "Type should match");
    ASSERT_EQUAL(deserialized.GetPayloadSize(), original.GetPayloadSize(), "Payload size should match");
    ASSERT_EQUAL(deserialized.GetPayloadString(), original_payload, "Payload should match");
}

/**
 * @brief Test round-trip with binary payload containing null bytes
 */
TEST(PeerMessage_RoundTripBinaryWithNulls) {
    std::vector<uint8_t> original_payload = {0x00, 0xFF, 0x00, 0x42, 0x00};
    CPeerMessage original(MessageType::BLOCKS, original_payload);

    std::string serialized = original.Serialize();

    CPeerMessage deserialized;
    bool result = deserialized.Deserialize(serialized);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_TRUE(CompareMessageType(deserialized.GetType(), original.GetType()), "Type should match");
    ASSERT_EQUAL(deserialized.GetPayloadSize(), original.GetPayloadSize(), "Payload size should match");
    ASSERT_TRUE(CompareByteVectors(deserialized.GetPayloadBytes(), original_payload), "Binary payload should match");
}

/**
 * @brief Test MessageType constants are correct strings
 */
TEST(PeerMessage_TypeStrings) {
    ASSERT_EQUAL(MessageType::PING, std::string("ping"), "PING string");
    ASSERT_EQUAL(MessageType::PONG, std::string("pong"), "PONG string");
    ASSERT_EQUAL(MessageType::GET_PEERS, std::string("get_peers"), "GET_PEERS string");
    ASSERT_EQUAL(MessageType::PEERS, std::string("peers"), "PEERS string");
    ASSERT_EQUAL(MessageType::TX_IDS, std::string("tx_ids"), "TX_IDS string");
    ASSERT_EQUAL(MessageType::TXS, std::string("txs"), "TXS string");
    ASSERT_EQUAL(MessageType::BLOCKS, std::string("blocks"), "BLOCKS string");
    ASSERT_EQUAL(MessageType::GET_CHAIN, std::string("get_chain"), "GET_CHAIN string");
    ASSERT_EQUAL(MessageType::CHAIN_INFO, std::string("chain_info"), "CHAIN_INFO string");
    ASSERT_EQUAL(MessageType::UNKNOWN, std::string("unknown"), "UNKNOWN string");
}

/**
 * @brief Test SetType and SetPayload methods
 */
TEST(PeerMessage_SettersAndGetters) {
    CPeerMessage msg;

    // Initially UNKNOWN and invalid
    ASSERT_FALSE(msg.IsValid(), "Should be invalid initially");

    // Set type
    msg.SetType(MessageType::PING);
    ASSERT_TRUE(CompareMessageType(msg.GetType(), MessageType::PING), "Type should be PING");
    ASSERT_TRUE(msg.IsValid(), "Should be valid after setting type");

    // Set payload from string
    std::string str_payload = "test payload";
    msg.SetPayload(str_payload);
    ASSERT_EQUAL(msg.GetPayloadString(), str_payload, "String payload should match");
    ASSERT_EQUAL(msg.GetPayloadSize(), str_payload.size(), "Payload size should match");

    // Set payload from bytes
    std::vector<uint8_t> byte_payload = {0x01, 0x02, 0x03};
    msg.SetPayload(byte_payload);
    ASSERT_TRUE(CompareByteVectors(msg.GetPayloadBytes(), byte_payload), "Byte payload should match");
    ASSERT_EQUAL(msg.GetPayloadSize(), byte_payload.size(), "Payload size should match");
}

/**
 * @brief Test GetMinHeaderSize
 */
TEST(PeerMessage_GetMinHeaderSize) {
    // Minimum header size is 1 (type_length) + 0 (empty type) + 4 (payload_length) = 5 bytes
    ASSERT_EQUAL(CPeerMessage::GetMinHeaderSize(), (size_t)5, "Minimum header size should be 5 bytes");
}

/**
 * @brief Test all message types are valid except UNKNOWN
 */
TEST(PeerMessage_MessageTypeValidity) {
    CPeerMessage ping(MessageType::PING);
    CPeerMessage pong(MessageType::PONG);
    CPeerMessage get_peers(MessageType::GET_PEERS);
    CPeerMessage peers(MessageType::PEERS);
    CPeerMessage tx_ids(MessageType::TX_IDS);
    CPeerMessage txs(MessageType::TXS);
    CPeerMessage blocks(MessageType::BLOCKS);
    CPeerMessage get_chain(MessageType::GET_CHAIN);
    CPeerMessage chain_info(MessageType::CHAIN_INFO);
    CPeerMessage unknown(MessageType::UNKNOWN);

    ASSERT_TRUE(ping.IsValid(), "PING should be valid");
    ASSERT_TRUE(pong.IsValid(), "PONG should be valid");
    ASSERT_TRUE(get_peers.IsValid(), "GET_PEERS should be valid");
    ASSERT_TRUE(peers.IsValid(), "PEERS should be valid");
    ASSERT_TRUE(tx_ids.IsValid(), "TX_IDS should be valid");
    ASSERT_TRUE(txs.IsValid(), "TXS should be valid");
    ASSERT_TRUE(blocks.IsValid(), "BLOCKS should be valid");
    ASSERT_TRUE(get_chain.IsValid(), "GET_CHAIN should be valid");
    ASSERT_TRUE(chain_info.IsValid(), "CHAIN_INFO should be valid");
    ASSERT_FALSE(unknown.IsValid(), "UNKNOWN should be invalid");
}

/**
 * @brief Test serialization format consistency
 */
TEST(PeerMessage_SerializationFormat) {
    std::string payload = "ABC";
    CPeerMessage msg(MessageType::TXS, payload);
    std::string serialized = msg.Serialize();

    // Verify format: [1 byte type_length][3 bytes "txs"][4 bytes payload_length][3 bytes "ABC"]
    // Total: 1 + 3 + 4 + 3 = 11 bytes
    ASSERT_EQUAL(serialized.size(), (size_t)11, "Total size should be 11 bytes");

    // Byte 0: Type length
    ASSERT_EQUAL((uint8_t)serialized[0], (uint8_t)3, "Byte 0 should be type length (3)");

    // Bytes 1-3: Type string "txs"
    std::string type_str = serialized.substr(1, 3);
    ASSERT_EQUAL(type_str, std::string("txs"), "Type string should be 'txs'");

    // Bytes 4-7: Payload length (network byte order)
    uint32_t length_network;
    std::memcpy(&length_network, serialized.data() + 4, 4);
    uint32_t length = ntohl(length_network);
    ASSERT_EQUAL(length, (uint32_t)3, "Payload length should be 3");

    // Bytes 8-10: Payload
    std::string extracted_payload = serialized.substr(8, 3);
    ASSERT_EQUAL(extracted_payload, payload, "Payload should match");
}

/**
 * @brief Test empty string payload handling
 */
TEST(PeerMessage_EmptyStringPayload) {
    std::string empty_payload = "";
    CPeerMessage msg(MessageType::PING, empty_payload);

    ASSERT_EQUAL(msg.GetPayloadSize(), (size_t)0, "Empty string should have size 0");
    ASSERT_EQUAL(msg.GetPayloadString(), std::string(""), "Should return empty string");

    // Serialize and deserialize
    std::string serialized = msg.Serialize();
    CPeerMessage deserialized;
    ASSERT_TRUE(deserialized.Deserialize(serialized), "Should deserialize successfully");
    ASSERT_EQUAL(deserialized.GetPayloadSize(), (size_t)0, "Deserialized payload should be empty");
}

/**
 * @brief Test network byte order conversion
 */
TEST(PeerMessage_NetworkByteOrder) {
    // Create message with known size payload
    std::string payload(256, 'X'); // 256 bytes
    CPeerMessage msg(MessageType::BLOCKS, payload);
    std::string serialized = msg.Serialize();

    // Format: [1 byte type_length][6 bytes "blocks"][4 bytes payload_length][256 bytes payload]
    // Extract length field (bytes 7-10, after 1 byte type_length + 6 bytes "blocks")
    uint32_t length_network;
    std::memcpy(&length_network, serialized.data() + 7, 4);
    uint32_t length_host = ntohl(length_network);

    ASSERT_EQUAL(length_host, (uint32_t)256, "Payload length should be 256 in host byte order");

    // Verify deserialization handles byte order correctly
    CPeerMessage deserialized;
    ASSERT_TRUE(deserialized.Deserialize(serialized), "Should deserialize successfully");
    ASSERT_EQUAL(deserialized.GetPayloadSize(), (size_t)256, "Payload size should be 256");
}
