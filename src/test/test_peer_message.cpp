// ============= test_peer_message.cpp =============
#include "unit_test.h"
#include "peer/peer_message.h"
#include <cstring>

using namespace UnitTest;

// Helper function to compare message types
static bool CompareMessageType(EMessageType actual, EMessageType expected) {
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

    ASSERT_TRUE(CompareMessageType(msg.GetType(), EMessageType::UNKNOWN), "Default type should be UNKNOWN");
    ASSERT_EQUAL(msg.GetPayloadSize(), (size_t)0, "Default payload should be empty");
    ASSERT_FALSE(msg.IsValid(), "Default message should be invalid");
    ASSERT_EQUAL(msg.GetTypeString(), std::string("UNKNOWN"), "Default type string should be 'UNKNOWN'");
}

/**
 * @brief Test CPeerMessage constructor with type
 */
TEST(PeerMessage_ConstructorWithType) {
    CPeerMessage ping(EMessageType::PING);

    ASSERT_TRUE(CompareMessageType(ping.GetType(), EMessageType::PING), "Type should be PING");
    ASSERT_EQUAL(ping.GetPayloadSize(), (size_t)0, "Payload should be empty");
    ASSERT_TRUE(ping.IsValid(), "PING message should be valid");
    ASSERT_EQUAL(ping.GetTypeString(), std::string("PING"), "Type string should be 'PING'");
}

/**
 * @brief Test CPeerMessage constructor with type and string payload
 */
TEST(PeerMessage_ConstructorWithStringPayload) {
    std::string payload = "Hello, Peer!";
    CPeerMessage msg(EMessageType::TX, payload);

    ASSERT_TRUE(CompareMessageType(msg.GetType(), EMessageType::TX), "Type should be TX");
    ASSERT_EQUAL(msg.GetPayloadSize(), payload.size(), "Payload size should match");
    ASSERT_EQUAL(msg.GetPayloadString(), payload, "Payload string should match");
    ASSERT_TRUE(msg.IsValid(), "TX message should be valid");
}

/**
 * @brief Test CPeerMessage constructor with type and binary payload
 */
TEST(PeerMessage_ConstructorWithBinaryPayload) {
    std::vector<uint8_t> payload = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0xFD};
    CPeerMessage msg(EMessageType::BLOCK, payload);

    ASSERT_TRUE(CompareMessageType(msg.GetType(), EMessageType::BLOCK), "Type should be BLOCK");
    ASSERT_EQUAL(msg.GetPayloadSize(), payload.size(), "Payload size should match");
    ASSERT_TRUE(CompareByteVectors(msg.GetPayloadBytes(), payload), "Payload bytes should match");
}

/**
 * @brief Test serialization with empty payload
 */
TEST(PeerMessage_SerializeEmpty) {
    CPeerMessage ping(EMessageType::PING);
    std::string serialized = ping.Serialize();

    // Should be 5 bytes: 1 byte type + 4 bytes length (0)
    ASSERT_EQUAL(serialized.size(), (size_t)5, "Serialized size should be 5 bytes (header only)");
    ASSERT_EQUAL((uint8_t)serialized[0], (uint8_t)0, "First byte should be PING (0)");

    // Length should be 0 (4 bytes, all zero in network byte order)
    uint32_t length;
    std::memcpy(&length, serialized.data() + 1, 4);
    ASSERT_EQUAL(length, (uint32_t)0, "Length field should be 0");
}

/**
 * @brief Test serialization with small payload
 */
TEST(PeerMessage_SerializeSmallPayload) {
    std::string payload = "test";
    CPeerMessage msg(EMessageType::PONG, payload);
    std::string serialized = msg.Serialize();

    // Should be 5 + 4 = 9 bytes
    ASSERT_EQUAL(serialized.size(), (size_t)9, "Serialized size should be 9 bytes");
    ASSERT_EQUAL((uint8_t)serialized[0], (uint8_t)1, "First byte should be PONG (1)");

    // Extract and verify payload
    std::string extracted_payload = serialized.substr(5);
    ASSERT_EQUAL(extracted_payload, payload, "Payload should match");
}

/**
 * @brief Test serialization with large payload
 */
TEST(PeerMessage_SerializeLargePayload) {
    std::string large_payload(1000, 'X');
    CPeerMessage msg(EMessageType::TX, large_payload);
    std::string serialized = msg.Serialize();

    // Should be 5 + 1000 = 1005 bytes
    ASSERT_EQUAL(serialized.size(), (size_t)1005, "Serialized size should be 1005 bytes");
    ASSERT_EQUAL((uint8_t)serialized[0], (uint8_t)6, "First byte should be TX (6)");

    // Extract and verify payload
    std::string extracted_payload = serialized.substr(5);
    ASSERT_EQUAL(extracted_payload, large_payload, "Large payload should match");
}

/**
 * @brief Test deserialization with valid data
 */
TEST(PeerMessage_DeserializeValid) {
    // Create a simple PING message manually
    std::string data;
    data.push_back((char)0); // PING type
    // Length = 0 (4 bytes, network byte order)
    uint32_t length = 0;
    data.append(reinterpret_cast<const char*>(&length), 4);

    CPeerMessage msg;
    bool result = msg.Deserialize(data);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_TRUE(CompareMessageType(msg.GetType(), EMessageType::PING), "Type should be PING");
    ASSERT_EQUAL(msg.GetPayloadSize(), (size_t)0, "Payload should be empty");
}

/**
 * @brief Test deserialization with payload
 */
TEST(PeerMessage_DeserializeWithPayload) {
    std::string payload_data = "Hello";

    // Manually construct serialized message
    std::string data;
    data.push_back((char)1); // PONG type

    // Length = 5 (network byte order)
    uint32_t length = htonl(5);
    data.append(reinterpret_cast<const char*>(&length), 4);
    data.append(payload_data);

    CPeerMessage msg;
    bool result = msg.Deserialize(data);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_TRUE(CompareMessageType(msg.GetType(), EMessageType::PONG), "Type should be PONG");
    ASSERT_EQUAL(msg.GetPayloadSize(), (size_t)5, "Payload size should be 5");
    ASSERT_EQUAL(msg.GetPayloadString(), payload_data, "Payload should match");
}

/**
 * @brief Test deserialization with insufficient header data
 */
TEST(PeerMessage_DeserializeTooShort) {
    std::string data = "ABC"; // Only 3 bytes, need at least 5

    CPeerMessage msg;
    bool result = msg.Deserialize(data);

    ASSERT_FALSE(result, "Deserialization should fail with insufficient data");
}

/**
 * @brief Test deserialization with insufficient payload data
 */
TEST(PeerMessage_DeserializeInsufficientPayload) {
    std::string data;
    data.push_back((char)2); // GET_PEERS type

    // Claim length = 100, but only provide 5 bytes
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
    CPeerMessage original(EMessageType::GET_PEERS);

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
    CPeerMessage original(EMessageType::TX, original_payload);

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
    CPeerMessage original(EMessageType::BLOCK, original_payload);

    std::string serialized = original.Serialize();

    CPeerMessage deserialized;
    bool result = deserialized.Deserialize(serialized);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_TRUE(CompareMessageType(deserialized.GetType(), original.GetType()), "Type should match");
    ASSERT_EQUAL(deserialized.GetPayloadSize(), original.GetPayloadSize(), "Payload size should match");
    ASSERT_TRUE(CompareByteVectors(deserialized.GetPayloadBytes(), original_payload), "Binary payload should match");
}

/**
 * @brief Test TypeToString for all message types
 */
TEST(PeerMessage_TypeToString) {
    ASSERT_EQUAL(CPeerMessage::TypeToString(EMessageType::PING), std::string("PING"), "PING string");
    ASSERT_EQUAL(CPeerMessage::TypeToString(EMessageType::PONG), std::string("PONG"), "PONG string");
    ASSERT_EQUAL(CPeerMessage::TypeToString(EMessageType::GET_PEERS), std::string("GET_PEERS"), "GET_PEERS string");
    ASSERT_EQUAL(CPeerMessage::TypeToString(EMessageType::PEERS), std::string("PEERS"), "PEERS string");
    ASSERT_EQUAL(CPeerMessage::TypeToString(EMessageType::TX_IDS), std::string("TX_IDS"), "TX_IDS string");
    ASSERT_EQUAL(CPeerMessage::TypeToString(EMessageType::GET_TX), std::string("GET_TX"), "GET_TX string");
    ASSERT_EQUAL(CPeerMessage::TypeToString(EMessageType::TX), std::string("TX"), "TX string");
    ASSERT_EQUAL(CPeerMessage::TypeToString(EMessageType::GET_BLOCK), std::string("GET_BLOCK"), "GET_BLOCK string");
    ASSERT_EQUAL(CPeerMessage::TypeToString(EMessageType::BLOCK), std::string("BLOCK"), "BLOCK string");
    ASSERT_EQUAL(CPeerMessage::TypeToString(EMessageType::GET_CHAIN), std::string("GET_CHAIN"), "GET_CHAIN string");
    ASSERT_EQUAL(CPeerMessage::TypeToString(EMessageType::CHAIN_INFO), std::string("CHAIN_INFO"), "CHAIN_INFO string");
    ASSERT_EQUAL(CPeerMessage::TypeToString(EMessageType::UNKNOWN), std::string("UNKNOWN"), "UNKNOWN string");
}

/**
 * @brief Test SetType and SetPayload methods
 */
TEST(PeerMessage_SettersAndGetters) {
    CPeerMessage msg;

    // Initially UNKNOWN and invalid
    ASSERT_FALSE(msg.IsValid(), "Should be invalid initially");

    // Set type
    msg.SetType(EMessageType::PING);
    ASSERT_TRUE(CompareMessageType(msg.GetType(), EMessageType::PING), "Type should be PING");
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
 * @brief Test GetHeaderSize
 */
TEST(PeerMessage_GetHeaderSize) {
    ASSERT_EQUAL(CPeerMessage::GetHeaderSize(), (size_t)5, "Header size should be 5 bytes");
}

/**
 * @brief Test all message types are valid except UNKNOWN
 */
TEST(PeerMessage_MessageTypeValidity) {
    CPeerMessage ping(EMessageType::PING);
    CPeerMessage pong(EMessageType::PONG);
    CPeerMessage get_peers(EMessageType::GET_PEERS);
    CPeerMessage peers(EMessageType::PEERS);
    CPeerMessage tx_ids(EMessageType::TX_IDS);
    CPeerMessage get_tx(EMessageType::GET_TX);
    CPeerMessage tx(EMessageType::TX);
    CPeerMessage get_block(EMessageType::GET_BLOCK);
    CPeerMessage block(EMessageType::BLOCK);
    CPeerMessage get_chain(EMessageType::GET_CHAIN);
    CPeerMessage chain_info(EMessageType::CHAIN_INFO);
    CPeerMessage unknown(EMessageType::UNKNOWN);

    ASSERT_TRUE(ping.IsValid(), "PING should be valid");
    ASSERT_TRUE(pong.IsValid(), "PONG should be valid");
    ASSERT_TRUE(get_peers.IsValid(), "GET_PEERS should be valid");
    ASSERT_TRUE(peers.IsValid(), "PEERS should be valid");
    ASSERT_TRUE(tx_ids.IsValid(), "TX_IDS should be valid");
    ASSERT_TRUE(get_tx.IsValid(), "GET_TX should be valid");
    ASSERT_TRUE(tx.IsValid(), "TX should be valid");
    ASSERT_TRUE(get_block.IsValid(), "GET_BLOCK should be valid");
    ASSERT_TRUE(block.IsValid(), "BLOCK should be valid");
    ASSERT_TRUE(get_chain.IsValid(), "GET_CHAIN should be valid");
    ASSERT_TRUE(chain_info.IsValid(), "CHAIN_INFO should be valid");
    ASSERT_FALSE(unknown.IsValid(), "UNKNOWN should be invalid");
}

/**
 * @brief Test serialization format consistency
 */
TEST(PeerMessage_SerializationFormat) {
    std::string payload = "ABC";
    CPeerMessage msg(EMessageType::TX, payload);
    std::string serialized = msg.Serialize();

    // Verify format: [1 byte type][4 bytes length][N bytes payload]
    ASSERT_EQUAL(serialized.size(), (size_t)8, "Total size should be 8 bytes");

    // Byte 0: Type
    ASSERT_EQUAL((uint8_t)serialized[0], (uint8_t)6, "Byte 0 should be TX type (6)");

    // Bytes 1-4: Length (network byte order)
    uint32_t length_network;
    std::memcpy(&length_network, serialized.data() + 1, 4);
    uint32_t length = ntohl(length_network);
    ASSERT_EQUAL(length, (uint32_t)3, "Length should be 3");

    // Bytes 5-7: Payload
    std::string extracted_payload = serialized.substr(5, 3);
    ASSERT_EQUAL(extracted_payload, payload, "Payload should match");
}

/**
 * @brief Test empty string payload handling
 */
TEST(PeerMessage_EmptyStringPayload) {
    std::string empty_payload = "";
    CPeerMessage msg(EMessageType::PING, empty_payload);

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
    CPeerMessage msg(EMessageType::BLOCK, payload);
    std::string serialized = msg.Serialize();

    // Extract length field (bytes 1-4)
    uint32_t length_network;
    std::memcpy(&length_network, serialized.data() + 1, 4);
    uint32_t length_host = ntohl(length_network);

    ASSERT_EQUAL(length_host, (uint32_t)256, "Length should be 256 in host byte order");

    // Verify deserialization handles byte order correctly
    CPeerMessage deserialized;
    ASSERT_TRUE(deserialized.Deserialize(serialized), "Should deserialize successfully");
    ASSERT_EQUAL(deserialized.GetPayloadSize(), (size_t)256, "Payload size should be 256");
}
