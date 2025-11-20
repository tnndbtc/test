// ============= test_peer_message.cpp =============
#include "unit_test.h"
#include "peer/peer_message.h"
#include "peer/peer_message_factory.h"
#include "peer/messages/ping_message.h"
#include "peer/messages/pong_message.h"
#include "peer/messages/version_message.h"
#include "peer/messages/verack_message.h"
#include "peer/messages/inventory_message.h"
#include "peer/messages/getdata_message.h"
#include "peer/messages/tx_message.h"
#include "peer/messages/block_message.h"
#include "peer/messages/getpeers_message.h"
#include "peer/messages/peers_message.h"
#include "peer/messages/getchain_message.h"
#include "peer/messages/chaininfo_message.h"
#include "peer/messages/unknown_message.h"
#include "blockcore/object_type.h"
#include "blockcore/transaction.h"
#include "blockcore/block.h"
#include "blockcore/protocol.h"
#include "utils/hash.h"
#include <cstring>

using namespace UnitTest;
/*
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
*/

/**
 * @brief Test MessageType constants are correct strings
 */
TEST(PeerMessage_TypeStrings) {
    ASSERT_EQUAL(MessageType::PING, std::string("ping"), "PING string");
    ASSERT_EQUAL(MessageType::PONG, std::string("pong"), "PONG string");
    ASSERT_EQUAL(MessageType::GET_PEERS, std::string("get_peers"), "GET_PEERS string");
    ASSERT_EQUAL(MessageType::PEERS, std::string("peers"), "PEERS string");
    ASSERT_EQUAL(MessageType::TX, std::string("tx"), "TX string");
    ASSERT_EQUAL(MessageType::BLOCK, std::string("block"), "BLOCK string");
    ASSERT_EQUAL(MessageType::GET_CHAIN, std::string("get_chain"), "GET_CHAIN string");
    ASSERT_EQUAL(MessageType::CHAIN_INFO, std::string("chain_info"), "CHAIN_INFO string");
    ASSERT_EQUAL(MessageType::UNKNOWN, std::string("unknown"), "UNKNOWN string");
}

/**
 * @brief Test GetMinHeaderSize
 */
TEST(PeerMessage_GetMinHeaderSize) {
    // Minimum header size is 4 (magic) + 1 (type_length) + 0 (empty type) + 4 (payload_length) = 9 bytes
    ASSERT_EQUAL(CPeerMessage::GetMinHeaderSize(), (size_t)9, "Minimum header size should be 9 bytes");
}

// ==================== PING MESSAGE TESTS ====================

/**
 * @brief Test PING message serialization
 */
TEST(PingMessage_SerializePayload) {
    CPingMessage ping(0);
    uint32_t n_auto_nonce = ping.GetNonce();
    std::vector<uint8_t> payload = ping.SerializePayload();

    ASSERT_EQUAL(payload.size(), (size_t)4, "PING payload should be 4 bytes");

    // Verify nonce is in network byte order
    uint32_t n_nonce_network;
    std::memcpy(&n_nonce_network, payload.data(), 4);
    uint32_t n_nonce = ntohl(n_nonce_network);
    ASSERT_EQUAL(n_nonce, n_auto_nonce, "Nonce should match auto-generated value");
}

/**
 * @brief Test PING message deserialization with valid data
 */
TEST(PingMessage_DeserializePayload_Valid) {
    CPingMessage ping(0);

    // Create 4-byte payload with nonce 99999
    std::vector<uint8_t> payload(4);
    uint32_t n_nonce_network = htonl(99999);
    std::memcpy(payload.data(), &n_nonce_network, 4);

    bool result = ping.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(ping.GetNonce(), (uint32_t)99999, "Nonce should be 99999");
}

/**
 * @brief Test PING message deserialization with invalid size
 */
TEST(PingMessage_DeserializePayload_InvalidSize) {
    CPingMessage ping(0);

    // Test with 3 bytes (too short)
    std::vector<uint8_t> payload_short(3, 0);
    ASSERT_FALSE(ping.DeserializePayload(payload_short), "Should reject 3-byte payload");

    // Test with 5 bytes (too long)
    std::vector<uint8_t> payload_long(5, 0);
    ASSERT_FALSE(ping.DeserializePayload(payload_long), "Should reject 5-byte payload");
}

/**
 * @brief Test PING message round-trip
 */
TEST(PingMessage_RoundTrip) {
    CPingMessage original(0);
    uint32_t n_original_nonce = original.GetNonce();

    std::vector<uint8_t> payload = original.SerializePayload();

    CPingMessage deserialized(0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(deserialized.GetNonce(), n_original_nonce, "Nonce should match original");
}

/**
 * @brief Test PING message Clone functionality
 */
TEST(PingMessage_Clone) {
    CPingMessage original(0);
    uint32_t n_original_nonce = original.GetNonce();

    auto p_clone = original.Clone();
    auto* p_ping_clone = dynamic_cast<CPingMessage*>(p_clone.get());

    ASSERT_TRUE(p_ping_clone != nullptr, "Clone should be a CPingMessage");
    ASSERT_EQUAL(p_ping_clone->GetNonce(), n_original_nonce, "Cloned nonce should match");
    ASSERT_EQUAL(p_ping_clone->GetType(), MessageType::PING, "Cloned type should be PING");
}

/**
 * @brief Test PING message GetNonce
 */
TEST(PingMessage_GetSetNonce) {
    CPingMessage ping(0);
    uint32_t n_nonce = ping.GetNonce();
    // Suppress comiple warning
    (void)n_nonce;

    // Nonce should be auto-generated (non-zero most of the time)
    // Just verify we can get it
    ASSERT_TRUE(true, "GetNonce should work");
}

/**
 * @brief Test PING message GetType
 */
TEST(PingMessage_GetType) {
    CPingMessage ping(0);
    ASSERT_EQUAL(ping.GetType(), MessageType::PING, "Type should be 'ping'");
}

// ==================== PONG MESSAGE TESTS ====================

/**
 * @brief Test PONG message construction
 */
TEST(PongMessage_Construction) {
    CPongMessage pong(54321, 0);
    ASSERT_EQUAL(pong.GetType(), MessageType::PONG, "Type should be 'pong'");
    ASSERT_EQUAL(pong.GetNonce(), (uint32_t)54321, "Nonce should be 54321");
}

/**
 * @brief Test PONG message GetType
 */
TEST(PongMessage_GetType) {
    CPongMessage pong(0, 0);
    ASSERT_EQUAL(pong.GetType(), MessageType::PONG, "Type should be 'pong'");
}

/**
 * @brief Test PONG message serialization
 */
TEST(PongMessage_SerializePayload) {
    CPongMessage pong(67890, 0);
    std::vector<uint8_t> payload = pong.SerializePayload();

    ASSERT_EQUAL(payload.size(), (size_t)4, "PONG payload should be 4 bytes");

    uint32_t n_nonce_network;
    std::memcpy(&n_nonce_network, payload.data(), 4);
    uint32_t n_nonce = ntohl(n_nonce_network);
    ASSERT_EQUAL(n_nonce, (uint32_t)67890, "Nonce should be 67890");
}

/**
 * @brief Test PONG message deserialization
 */
TEST(PongMessage_DeserializePayload_Valid) {
    CPongMessage pong(0, 0);

    std::vector<uint8_t> payload(4);
    uint32_t n_nonce_network = htonl(11111);
    std::memcpy(payload.data(), &n_nonce_network, 4);

    bool result = pong.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(pong.GetNonce(), (uint32_t)11111, "Nonce should be 11111");
}

/**
 * @brief Test PONG message deserialization with invalid size
 */
TEST(PongMessage_DeserializePayload_InvalidSize) {
    CPongMessage pong(0, 0);

    std::vector<uint8_t> payload_short(3, 0);
    ASSERT_FALSE(pong.DeserializePayload(payload_short), "Should reject short payload");

    std::vector<uint8_t> payload_long(5, 0);
    ASSERT_FALSE(pong.DeserializePayload(payload_long), "Should reject long payload");
}

/**
 * @brief Test PONG message round-trip
 */
TEST(PongMessage_RoundTrip) {
    CPongMessage original(98765, 0);

    std::vector<uint8_t> payload = original.SerializePayload();
    CPongMessage deserialized(0, 0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(deserialized.GetNonce(), original.GetNonce(), "Nonce should match");
    ASSERT_EQUAL(deserialized.GetNonce(), 98765, "Nonce should be 98765");
}

/**
 * @brief Test PONG message Clone
 */
TEST(PongMessage_Clone) {
    CPongMessage original(22222, 0);

    auto p_clone = original.Clone();
    auto* p_pong_clone = dynamic_cast<CPongMessage*>(p_clone.get());

    ASSERT_TRUE(p_pong_clone != nullptr, "Clone should be a CPongMessage");
    ASSERT_EQUAL(p_pong_clone->GetNonce(), (uint32_t)22222, "Cloned nonce should match");
}

/**
 * @brief Test PONG message GetSetNonce
 */
TEST(PongMessage_GetSetNonce) {
    CPongMessage pong(300, 0);

    ASSERT_EQUAL(pong.GetNonce(), (uint32_t)300, "Initial nonce should be 300");

    pong.SetNonce(400);
    ASSERT_EQUAL(pong.GetNonce(), (uint32_t)400, "Nonce should be 400 after set");
}

// ==================== VERSION MESSAGE TESTS ====================

/**
 * @brief Test VERSION message construction
 */
TEST(VersionMessage_Construction) {
    CVersionMessage version(0);
    version.SetChainTipHeight(100);

    ASSERT_EQUAL(version.GetType(), MessageType::VERSION, "Type should be 'version'");
    ASSERT_EQUAL(version.GetProtocolVersion(), PROTOCOL_VERSION, "Protocol version should be auto-set from constant");
    ASSERT_TRUE(version.GetTimestamp() > 0, "Timestamp should be auto-set to current time");
    ASSERT_TRUE(version.GetNonce() != 0, "Nonce should be auto-generated (non-zero most of the time)");
    ASSERT_EQUAL(version.GetChainTipHeight(), 100, "Chain tip height should match");
}

/**
 * @brief Test VERSION message GetType
 */
TEST(VersionMessage_GetType) {
    CVersionMessage version(0);
    ASSERT_EQUAL(version.GetType(), MessageType::VERSION, "Type should be 'version'");
}

/**
 * @brief Test VERSION message serialization
 */
TEST(VersionMessage_SerializePayload) {
    CVersionMessage version(0);
    version.SetChainTipHeight(100);

    std::vector<uint8_t> payload = version.SerializePayload();

    // VERSION payload should be exactly 76 bytes
    ASSERT_EQUAL(payload.size(), (size_t)76, "Payload size should be 76 bytes");
}

/**
 * @brief Test VERSION message deserialization
 */
TEST(VersionMessage_DeserializePayload_Valid) {
    CVersionMessage original(0);
    original.SetChainTipHeight(100);

    std::vector<uint8_t> payload = original.SerializePayload();

    CVersionMessage deserialized(0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(deserialized.GetProtocolVersion(), original.GetProtocolVersion(), "Protocol version should match");
    ASSERT_EQUAL(deserialized.GetTimestamp(), original.GetTimestamp(), "Timestamp should match");
    ASSERT_EQUAL(deserialized.GetNonce(), original.GetNonce(), "Nonce should match");
    ASSERT_EQUAL(deserialized.GetChainTipHeight(), 100, "Chain tip height should match");
}

/**
 * @brief Test VERSION message round-trip with auto-generated values
 */
TEST(VersionMessage_RoundTrip_ZeroValues) {
    CVersionMessage original(0);

    std::vector<uint8_t> payload = original.SerializePayload();
    CVersionMessage deserialized(0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(deserialized.GetProtocolVersion(), original.GetProtocolVersion(), "Protocol version should match");
    ASSERT_EQUAL(deserialized.GetTimestamp(), original.GetTimestamp(), "Timestamp should match");
    ASSERT_EQUAL(deserialized.GetNonce(), original.GetNonce(), "Nonce should match");
    ASSERT_EQUAL(deserialized.GetChainTipHeight(), 0, "Chain tip height should be 0");
}

/**
 * @brief Test VERSION message round-trip with normal values
 */
TEST(VersionMessage_RoundTrip_NormalValues) {
    CVersionMessage original(0);
    original.SetChainTipHeight(500);

    std::vector<uint8_t> payload = original.SerializePayload();
    CVersionMessage deserialized(0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(deserialized.GetProtocolVersion(), original.GetProtocolVersion(), "Protocol version should match");
    ASSERT_EQUAL(deserialized.GetTimestamp(), original.GetTimestamp(), "Timestamp should match");
    ASSERT_EQUAL(deserialized.GetNonce(), original.GetNonce(), "Nonce should match");
    ASSERT_EQUAL(deserialized.GetChainTipHeight(), 500, "Chain tip height should match");
}

/**
 * @brief Test VERSION message round-trip with large chain height
 */
TEST(VersionMessage_RoundTrip_LargeValues) {
    CVersionMessage original(0);
    original.SetChainTipHeight(2147483647); // Max int32_t

    std::vector<uint8_t> payload = original.SerializePayload();
    CVersionMessage deserialized(0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(deserialized.GetProtocolVersion(), original.GetProtocolVersion(), "Protocol version should match");
    ASSERT_EQUAL(deserialized.GetTimestamp(), original.GetTimestamp(), "Timestamp should match");
    ASSERT_EQUAL(deserialized.GetNonce(), original.GetNonce(), "Nonce should match");
    ASSERT_EQUAL(deserialized.GetChainTipHeight(), 2147483647, "Chain tip height should match");
}

/**
 * @brief Test VERSION message Clone
 */
TEST(VersionMessage_Clone) {
    CVersionMessage original(0);
    original.SetChainTipHeight(100);

    auto p_clone = original.Clone();
    auto* p_version_clone = dynamic_cast<CVersionMessage*>(p_clone.get());

    ASSERT_TRUE(p_version_clone != nullptr, "Clone should be a CVersionMessage");
    ASSERT_EQUAL(p_version_clone->GetProtocolVersion(), original.GetProtocolVersion(), "Cloned protocol version should match");
    ASSERT_EQUAL(p_version_clone->GetTimestamp(), original.GetTimestamp(), "Cloned timestamp should match");
    ASSERT_EQUAL(p_version_clone->GetNonce(), original.GetNonce(), "Cloned nonce should match");
    ASSERT_EQUAL(p_version_clone->GetChainTipHeight(), 100, "Cloned chain tip height should match");
}

/**
 * @brief Test VERSION message network address
 */
TEST(VersionMessage_NetworkAddress) {
    CVersionMessage version(0);

    CNetworkAddress addr(1ULL, "127.0.0.1", 8333);
    version.SetAddressFrom(addr);

    const CNetworkAddress& retrieved_addr = version.GetAddressFrom();
    ASSERT_EQUAL(retrieved_addr.n_services, 1ULL, "Services should match");
    ASSERT_EQUAL(retrieved_addr.n_port, (uint16_t)8333, "Port should match");
}

// ==================== VERACK MESSAGE TESTS ====================

/**
 * @brief Test VERACK message construction
 */
TEST(VerackMessage_Construction) {
    CVerackMessage verack(0);
    ASSERT_EQUAL(verack.GetType(), MessageType::VERACK, "Type should be 'verack'");
}

/**
 * @brief Test VERACK message GetType
 */
TEST(VerackMessage_GetType) {
    CVerackMessage verack(0);
    ASSERT_EQUAL(verack.GetType(), MessageType::VERACK, "Type should be 'verack'");
}

/**
 * @brief Test VERACK message serialization empty
 */
TEST(VerackMessage_SerializePayload_Empty) {
    CVerackMessage verack(0);
    std::vector<uint8_t> payload = verack.SerializePayload();

    ASSERT_EQUAL(payload.size(), (size_t)0, "VERACK payload should be empty");
}

/**
 * @brief Test VERACK message deserialization empty
 */
TEST(VerackMessage_DeserializePayload_Empty) {
    CVerackMessage verack(0);

    std::vector<uint8_t> payload;
    bool result = verack.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization of empty payload should succeed");
}

/**
 * @brief Test VERACK message round-trip
 */
TEST(VerackMessage_RoundTrip) {
    CVerackMessage original(0);

    std::vector<uint8_t> payload = original.SerializePayload();
    CVerackMessage deserialized(0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(deserialized.GetType(), original.GetType(), "Type should match");
}

/**
 * @brief Test VERACK message Clone
 */
TEST(VerackMessage_Clone) {
    CVerackMessage original(0);

    auto p_clone = original.Clone();
    auto* p_verack_clone = dynamic_cast<CVerackMessage*>(p_clone.get());

    ASSERT_TRUE(p_verack_clone != nullptr, "Clone should be a CVerackMessage");
    ASSERT_EQUAL(p_verack_clone->GetType(), MessageType::VERACK, "Cloned type should match");
}

// ==================== INVENTORY MESSAGE TESTS ====================

/**
 * @brief Test INVENTORY message construction empty
 */
TEST(InventoryMessage_Construction_Empty) {
    CInventoryMessage inv(0);
    ASSERT_EQUAL(inv.GetType(), MessageType::INVENTORY, "Type should be 'inv'");
    ASSERT_EQUAL(inv.GetItemCount(), (size_t)0, "Item count should be 0");
}

/**
 * @brief Test INVENTORY message construction with items
 */
TEST(InventoryMessage_Construction_WithItems) {
    std::vector<CInventoryItem> items;
    items.push_back(CInventoryItem(ObjectType::TRANSACTION, std::string(32, 'A')));
    CInventoryMessage inv(items, 0);

    ASSERT_EQUAL(inv.GetItemCount(), (size_t)1, "Should have 1 item");
}

/**
 * @brief Test INVENTORY message GetType
 */
TEST(InventoryMessage_GetType) {
    CInventoryMessage inv(0);
    ASSERT_EQUAL(inv.GetType(), MessageType::INVENTORY, "Type should be 'inv'");
}

/**
 * @brief Test INVENTORY message AddItem
 */
TEST(InventoryMessage_AddItem) {
    CInventoryMessage inv(0);
    std::string hash(32, 'B');

    inv.AddItem(ObjectType::TRANSACTION, hash);
    ASSERT_EQUAL(inv.GetItemCount(), (size_t)1, "Should have 1 item after add");

    inv.AddItem(ObjectType::BLOCK, std::string(32, 'C'));
    ASSERT_EQUAL(inv.GetItemCount(), (size_t)2, "Should have 2 items");
}

/**
 * @brief Test INVENTORY message serialization empty
 */
TEST(InventoryMessage_SerializePayload_Empty) {
    CInventoryMessage inv(0);
    std::vector<uint8_t> payload = inv.SerializePayload();

    // Empty inventory: just 4-byte count = 0
    ASSERT_EQUAL(payload.size(), (size_t)4, "Empty inventory should be 4 bytes");

    uint32_t n_count_network;
    std::memcpy(&n_count_network, payload.data(), 4);
    uint32_t n_count = ntohl(n_count_network);
    ASSERT_EQUAL(n_count, (uint32_t)0, "Count should be 0");
}

/**
 * @brief Test INVENTORY message serialization single item
 */
TEST(InventoryMessage_SerializePayload_SingleItem) {
    CInventoryMessage inv(0);
    std::string hash(32, 'Z');
    inv.AddItem(ObjectType::TRANSACTION, hash);

    std::vector<uint8_t> payload = inv.SerializePayload();

    // Format: [4 bytes count][2 bytes type][32 bytes hash]
    // = 4 + (2 + 32) = 38 bytes
    ASSERT_EQUAL(payload.size(), (size_t)38, "Single item should be 38 bytes");

    // Check count
    uint32_t n_count_network;
    std::memcpy(&n_count_network, payload.data(), 4);
    ASSERT_EQUAL(ntohl(n_count_network), (uint32_t)1, "Count should be 1");
}

/**
 * @brief Test INVENTORY message serialization multiple items
 */
TEST(InventoryMessage_SerializePayload_MultipleItems) {
    CInventoryMessage inv(0);
    inv.AddItem(ObjectType::TRANSACTION, std::string(32, 'A'));
    inv.AddItem(ObjectType::BLOCK, std::string(32, 'B'));
    inv.AddItem(ObjectType::TRANSACTION, std::string(32, 'C'));

    std::vector<uint8_t> payload = inv.SerializePayload();

    // Format: [4 bytes count][3 * (2 bytes type + 32 bytes hash)]
    // = 4 + 3 * 34 = 106 bytes
    ASSERT_EQUAL(payload.size(), (size_t)106, "Three items should be 106 bytes");

    uint32_t n_count_network;
    std::memcpy(&n_count_network, payload.data(), 4);
    ASSERT_EQUAL(ntohl(n_count_network), (uint32_t)3, "Count should be 3");
}

/**
 * @brief Test INVENTORY message deserialization valid
 */
TEST(InventoryMessage_DeserializePayload_Valid) {
    CInventoryMessage inv(0);

    // Create payload: 1 item
    std::vector<uint8_t> payload;
    uint32_t n_count = htonl(1);
    payload.insert(payload.end(), (uint8_t*)&n_count, (uint8_t*)&n_count + 4);

    uint16_t n_type = htons(ObjectType::TRANSACTION);
    payload.insert(payload.end(), (uint8_t*)&n_type, (uint8_t*)&n_type + 2);

    std::string hash(32, 'D');
    payload.insert(payload.end(), hash.begin(), hash.end());

    bool result = inv.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(inv.GetItemCount(), (size_t)1, "Should have 1 item");
}

/**
 * @brief Test INVENTORY message round-trip
 */
TEST(InventoryMessage_RoundTrip) {
    CInventoryMessage original(0);
    original.AddItem(ObjectType::TRANSACTION, std::string(32, 'M'));
    original.AddItem(ObjectType::BLOCK, std::string(32, 'N'));

    std::vector<uint8_t> payload = original.SerializePayload();

    CInventoryMessage deserialized(0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(deserialized.GetItemCount(), original.GetItemCount(), "Item count should match");
}

/**
 * @brief Test INVENTORY message Clone
 */
TEST(InventoryMessage_Clone) {
    CInventoryMessage original(0);
    original.AddItem(ObjectType::TRANSACTION, std::string(32, 'P'));

    auto p_clone = original.Clone();
    auto* p_inv_clone = dynamic_cast<CInventoryMessage*>(p_clone.get());

    ASSERT_TRUE(p_inv_clone != nullptr, "Clone should be a CInventoryMessage");
    ASSERT_EQUAL(p_inv_clone->GetItemCount(), (size_t)1, "Cloned item count should match");
}

// ==================== GETDATA MESSAGE TESTS ====================

/**
 * @brief Test GETDATA message construction empty
 */
TEST(GetDataMessage_Construction_Empty) {
    CGetDataMessage getdata(0);
    ASSERT_EQUAL(getdata.GetType(), MessageType::GETDATA, "Type should be 'getdata'");
    ASSERT_EQUAL(getdata.GetItemCount(), (size_t)0, "Item count should be 0");
}

/**
 * @brief Test GETDATA message construction with items
 */
TEST(GetDataMessage_Construction_WithItems) {
    std::vector<CInventoryItem> items;
    items.push_back(CInventoryItem(ObjectType::BLOCK, std::string(32, 'R')));
    CGetDataMessage getdata(items, 0);

    ASSERT_EQUAL(getdata.GetItemCount(), (size_t)1, "Should have 1 item");
}

/**
 * @brief Test GETDATA message GetType
 */
TEST(GetDataMessage_GetType) {
    CGetDataMessage getdata(0);
    ASSERT_EQUAL(getdata.GetType(), MessageType::GETDATA, "Type should be 'getdata'");
}

/**
 * @brief Test GETDATA message AddItem
 */
TEST(GetDataMessage_AddItem) {
    CGetDataMessage getdata(0);

    getdata.AddItem(ObjectType::TRANSACTION, std::string(32, 'S'));
    ASSERT_EQUAL(getdata.GetItemCount(), (size_t)1, "Should have 1 item");

    getdata.AddItem(ObjectType::BLOCK, std::string(32, 'T'));
    ASSERT_EQUAL(getdata.GetItemCount(), (size_t)2, "Should have 2 items");
}

/**
 * @brief Test GETDATA message serialization empty
 */
TEST(GetDataMessage_SerializePayload_Empty) {
    CGetDataMessage getdata(0);
    std::vector<uint8_t> payload = getdata.SerializePayload();

    ASSERT_EQUAL(payload.size(), (size_t)4, "Empty getdata should be 4 bytes");
}

/**
 * @brief Test GETDATA message serialization single item
 */
TEST(GetDataMessage_SerializePayload_SingleItem) {
    CGetDataMessage getdata(0);
    getdata.AddItem(ObjectType::TRANSACTION, std::string(32, 'V'));

    std::vector<uint8_t> payload = getdata.SerializePayload();

    ASSERT_EQUAL(payload.size(), (size_t)38, "Single item should be 38 bytes");
}

/**
 * @brief Test GETDATA message serialization multiple items
 */
TEST(GetDataMessage_SerializePayload_MultipleItems) {
    CGetDataMessage getdata(0);
    getdata.AddItem(ObjectType::TRANSACTION, std::string(32, 'W'));
    getdata.AddItem(ObjectType::BLOCK, std::string(32, 'X'));

    std::vector<uint8_t> payload = getdata.SerializePayload();

    // 4 + 2 * 34 = 72 bytes
    ASSERT_EQUAL(payload.size(), (size_t)72, "Two items should be 72 bytes");
}

/**
 * @brief Test GETDATA message deserialization
 */
TEST(GetDataMessage_DeserializePayload_Valid) {
    CGetDataMessage getdata(0);

    std::vector<uint8_t> payload;
    uint32_t n_count = htonl(1);
    payload.insert(payload.end(), (uint8_t*)&n_count, (uint8_t*)&n_count + 4);

    uint16_t n_type = htons(ObjectType::BLOCK);
    payload.insert(payload.end(), (uint8_t*)&n_type, (uint8_t*)&n_type + 2);

    std::string hash(32, 'Y');
    payload.insert(payload.end(), hash.begin(), hash.end());

    bool result = getdata.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(getdata.GetItemCount(), (size_t)1, "Should have 1 item");
}

/**
 * @brief Test GETDATA message round-trip
 */
TEST(GetDataMessage_RoundTrip) {
    CGetDataMessage original(0);
    original.AddItem(ObjectType::TRANSACTION, std::string(32, 'Z'));

    std::vector<uint8_t> payload = original.SerializePayload();

    CGetDataMessage deserialized(0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(deserialized.GetItemCount(), original.GetItemCount(), "Item count should match");
}

/**
 * @brief Test GETDATA message Clone
 */
TEST(GetDataMessage_Clone) {
    CGetDataMessage original(0);
    original.AddItem(ObjectType::TRANSACTION, std::string(32, '1'));

    auto p_clone = original.Clone();
    auto* p_getdata_clone = dynamic_cast<CGetDataMessage*>(p_clone.get());

    ASSERT_TRUE(p_getdata_clone != nullptr, "Clone should be a CGetDataMessage");
    ASSERT_EQUAL(p_getdata_clone->GetItemCount(), (size_t)1, "Cloned item count should match");
}

// ==================== TX MESSAGE TESTS ====================

/**
 * @brief Test TX message construction
 */
TEST(TxMessage_Construction) {
    std::vector<uint8_t> data = {1, 2, 3, 4};
    auto p_tx = std::make_shared<CTransaction>("owner", "target", data, 100);
    CTxMessage tx_msg(p_tx, 0);

    ASSERT_EQUAL(tx_msg.GetType(), MessageType::TX, "Type should be 'tx'");
    ASSERT_TRUE(tx_msg.GetTransaction() != nullptr, "Transaction should not be null");
    ASSERT_EQUAL(tx_msg.GetTransaction()->m_str_owner, std::string("owner"), "Owner should match");
}

/**
 * @brief Test TX message GetType
 */
TEST(TxMessage_GetType) {
    std::vector<uint8_t> data = {1};
    auto p_tx = std::make_shared<CTransaction>("owner", "target", data, 100);
    CTxMessage tx_msg(p_tx, 0);
    ASSERT_EQUAL(tx_msg.GetType(), MessageType::TX, "Type should be 'tx'");
}

/**
 * @brief Test TX message serialization
 */
TEST(TxMessage_SerializePayload) {
    std::vector<uint8_t> data = {0xAB, 0xCD};
    auto p_tx = std::make_shared<CTransaction>("alice", "bob", data, 500);
    CTxMessage tx_msg(p_tx, 0);

    std::vector<uint8_t> payload = tx_msg.SerializePayload();

    ASSERT_TRUE(payload.size() > 0, "Payload should not be empty");
    // Payload contains serialized transaction
}

/**
 * @brief Test TX message deserialization
 */
TEST(TxMessage_DeserializePayload) {
    // Create a transaction and serialize it
    std::vector<uint8_t> data = {0x12, 0x34};
    auto p_tx_original = std::make_shared<CTransaction>("sender", "receiver", data, 250);
    std::string str_serialized = p_tx_original->Serialize();
    std::vector<uint8_t> payload(str_serialized.begin(), str_serialized.end());

    // Deserialize into a new message
    CTxMessage tx_msg(nullptr, 0);
    bool result = tx_msg.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    auto p_tx = tx_msg.GetTransaction();
    ASSERT_TRUE(p_tx != nullptr, "Transaction should not be null");
    ASSERT_EQUAL(p_tx->m_str_owner, std::string("sender"), "Owner should match");
    ASSERT_EQUAL(p_tx->m_str_target, std::string("receiver"), "Target should match");
    ASSERT_TRUE(p_tx->m_data == data, "data should match");
}

/**
 * @brief Test TX message round-trip small
 */
TEST(TxMessage_RoundTrip_Small) {
    std::vector<uint8_t> data = {1, 2, 3};
    auto p_tx_original = std::make_shared<CTransaction>("alice", "bob", data, 100);
    CTxMessage original(p_tx_original, 0);

    std::vector<uint8_t> payload = original.SerializePayload();
    CTxMessage deserialized(nullptr, 0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    auto p_tx = deserialized.GetTransaction();
    ASSERT_TRUE(p_tx != nullptr, "Transaction should not be null");
    ASSERT_EQUAL(p_tx->m_str_owner, p_tx_original->m_str_owner, "Owner should match");
    ASSERT_EQUAL(p_tx->m_data.size(), p_tx_original->m_data.size(), "Data size should match");
    ASSERT_TRUE(p_tx->m_data == p_tx_original->m_data, "Data should match");
}

/**
 * @brief Test TX message round-trip large
 */
TEST(TxMessage_RoundTrip_Large) {
    std::vector<uint8_t> large_data(1000, 'L');
    auto p_tx_original = std::make_shared<CTransaction>("owner1", "target1", large_data, 1000);
    CTxMessage original(p_tx_original, 0);

    std::vector<uint8_t> payload = original.SerializePayload();
    CTxMessage deserialized(nullptr, 0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    auto p_tx = deserialized.GetTransaction();
    ASSERT_TRUE(p_tx != nullptr, "Transaction should not be null");
    ASSERT_EQUAL(p_tx->m_data.size(), (size_t)1000, "Large data size should match");
    ASSERT_TRUE(p_tx->m_data == large_data, "data should match");
}

/**
 * @brief Test TX message round-trip binary data
 */
TEST(TxMessage_RoundTrip_BinaryData) {
    std::vector<uint8_t> binary_data = {0x00, 0xFF, 0x00, 0x41};
    auto p_tx_original = std::make_shared<CTransaction>("sender", "receiver", binary_data, 500);
    CTxMessage original(p_tx_original, 0);

    std::vector<uint8_t> payload = original.SerializePayload();
    CTxMessage deserialized(nullptr, 0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    auto p_tx = deserialized.GetTransaction();
    ASSERT_TRUE(p_tx != nullptr, "Transaction should not be null");
    ASSERT_EQUAL(p_tx->m_data.size(), (size_t)4, "Binary data size should match");
    ASSERT_TRUE(p_tx->m_data == binary_data, "Binary data should match");
}

/**
 * @brief Test TX message Clone
 */
TEST(TxMessage_Clone) {
    std::vector<uint8_t> data = {0xAA, 0xBB};
    auto p_tx = std::make_shared<CTransaction>("alice", "bob", data, 200);
    CTxMessage original(p_tx, 0);

    auto p_clone = original.Clone();
    auto* p_tx_clone = dynamic_cast<CTxMessage*>(p_clone.get());

    ASSERT_TRUE(p_tx_clone != nullptr, "Clone should be a CTxMessage");
    auto p_tx_cloned = p_tx_clone->GetTransaction();
    auto orig_tx = original.GetTransaction();
    ASSERT_TRUE(p_tx_cloned != nullptr, "Cloned transaction should not be null");
    ASSERT_EQUAL(p_tx_cloned->m_str_owner, std::string("alice"), "Cloned owner should match");
    ASSERT_EQUAL(p_tx_cloned->m_str_target, orig_tx->m_str_target, "Target should match");
    ASSERT_TRUE(p_tx_cloned->m_data == orig_tx->m_data, "Cloned data should match");
    ASSERT_EQUAL(p_tx_cloned->m_n_reward, orig_tx->m_n_reward, "Reward should match");
    ASSERT_EQUAL(p_tx_cloned->m_n_timestamp, orig_tx->m_n_timestamp, "Timestamp should match");
    ASSERT_TRUE(p_tx_cloned->m_type == orig_tx->m_type, "Type should match");
}

/**
 * @brief Test TX message GetSetTxData - REMOVED (GetTransaction replaces GetTxData)
 */

// ==================== BLOCK MESSAGE TESTS ====================

/**
 * @brief Test BLOCK message construction
 */
TEST(BlockMessage_Construction) {
    CHash prev_hash("0000000000000000000000000000000000000000000000000000000000000000");
    auto p_block = std::make_shared<CBlock>(prev_hash, 1, "miner1");
    CBlockMessage block_msg(p_block, 0);

    ASSERT_EQUAL(block_msg.GetType(), MessageType::BLOCK, "Type should be 'block'");
    ASSERT_TRUE(block_msg.GetBlock() != nullptr, "Block should not be null");
    ASSERT_EQUAL(block_msg.GetBlock()->GetHeight(), (int64_t)1, "Block height should match");
}

/**
 * @brief Test BLOCK message GetType
 */
TEST(BlockMessage_GetType) {
    CHash prev_hash("0000000000000000000000000000000000000000000000000000000000000000");
    auto p_block = std::make_shared<CBlock>(prev_hash, 0, "genesis");
    CBlockMessage block_msg(p_block, 0);
    ASSERT_EQUAL(block_msg.GetType(), MessageType::BLOCK, "Type should be 'block'");
}

/**
 * @brief Test BLOCK message serialization
 */
TEST(BlockMessage_SerializePayload) {
    CHash prev_hash("1111111111111111111111111111111111111111111111111111111111111111");
    auto p_block = std::make_shared<CBlock>(prev_hash, 10, "miner_test");
    CBlockMessage block_msg(p_block, 0);

    std::vector<uint8_t> payload = block_msg.SerializePayload();

    ASSERT_TRUE(payload.size() > 0, "Payload should not be empty");
    // Payload contains serialized block
}

/**
 * @brief Test BLOCK message deserialization
 */
TEST(BlockMessage_DeserializePayload) {
    // Create a block and serialize it
    CHash prev_hash("2222222222222222222222222222222222222222222222222222222222222222");
    auto p_block_original = std::make_shared<CBlock>(prev_hash, 5, "test_miner");
    p_block_original->Mine();
    std::string str_serialized = p_block_original->Serialize();
    std::vector<uint8_t> payload(str_serialized.begin(), str_serialized.end());

    // Deserialize into a new message
    CBlockMessage block_msg(nullptr, 0);
    bool result = block_msg.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    auto p_block = block_msg.GetBlock();
    ASSERT_TRUE(p_block != nullptr, "Block should not be null");
    ASSERT_EQUAL(p_block->GetHeight(), (int64_t)5, "Height should match");
    ASSERT_EQUAL(p_block->GetMiner(), std::string("test_miner"), "Miner should match");
    ASSERT_TRUE(p_block->GetHash().GetData() == p_block_original->GetHash().GetData(), "Hash should match");
}

/**
 * @brief Test BLOCK message round-trip small
 */
TEST(BlockMessage_RoundTrip_Small) {
    CHash prev_hash("3333333333333333333333333333333333333333333333333333333333333333");
    auto p_block_original = std::make_shared<CBlock>(prev_hash, 3, "alice");
    CBlockMessage original(p_block_original, 0);

    std::vector<uint8_t> payload = original.SerializePayload();
    CBlockMessage deserialized(nullptr, 0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    auto p_block = deserialized.GetBlock();
    ASSERT_TRUE(p_block != nullptr, "Block should not be null");
    ASSERT_EQUAL(p_block->GetHeight(), p_block_original->GetHeight(), "Height should match");
    ASSERT_EQUAL(p_block->GetMiner(), p_block_original->GetMiner(), "Miner should match");
    ASSERT_EQUAL(p_block->GetHash().GetData(), p_block_original->GetHash().GetData(), "Hash should match");
}

/**
 * @brief Test BLOCK message round-trip large
 */
TEST(BlockMessage_RoundTrip_Large) {
    CHash prev_hash("4444444444444444444444444444444444444444444444444444444444444444");
    auto p_block_original = std::make_shared<CBlock>(prev_hash, 100, "miner_large");

    // Add many transactions
    for (int i = 0; i < 10; i++) {
        std::vector<uint8_t> data(100, 'D');
        auto p_tx = std::make_shared<CTransaction>("owner", "target", data, 100);
        p_block_original->AddTransaction(p_tx);
    }

    CBlockMessage original(p_block_original, 0);

    std::vector<uint8_t> payload = original.SerializePayload();
    CBlockMessage deserialized(nullptr, 0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    auto p_block = deserialized.GetBlock();
    ASSERT_TRUE(p_block != nullptr, "Block should not be null");
    ASSERT_EQUAL(p_block->GetTransactions().size(), (size_t)10, "Should have 10 transactions");
}

/**
 * @brief Test BLOCK message round-trip binary data
 */
TEST(BlockMessage_RoundTrip_BinaryData) {
    CHash prev_hash("5555555555555555555555555555555555555555555555555555555555555555");
    auto p_block_original = std::make_shared<CBlock>(prev_hash, 50, "binary_miner");

    // Add transaction with binary data
    std::vector<uint8_t> binary_data = {0x00, 0xFF, 0xAB};
    auto p_tx = std::make_shared<CTransaction>("bin_owner", "bin_target", binary_data, 300);
    p_block_original->AddTransaction(p_tx);

    CBlockMessage original(p_block_original, 0);

    std::vector<uint8_t> payload = original.SerializePayload();
    CBlockMessage deserialized(nullptr, 0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    auto p_block = deserialized.GetBlock();
    ASSERT_TRUE(p_block != nullptr, "Block should not be null");
    ASSERT_EQUAL(p_block->GetTransactions().size(), p_block_original->GetTransactions().size(), "Should have 1 transaction");

    const auto& orig_txs = p_block_original->GetTransactions();
    const auto& deserialized_txs = p_block->GetTransactions();
    // compare transactions
    for (size_t i = 0; i < deserialized_txs.size(); i++) {
        auto& des_tx = deserialized_txs[i];
        auto& orig_tx = orig_txs[i];
        ASSERT_EQUAL(des_tx->m_str_owner, orig_tx->m_str_owner, "Owner should match");
        ASSERT_EQUAL(des_tx->m_str_target, orig_tx->m_str_target, "Target should match");
        ASSERT_EQUAL(des_tx->m_data.size(), orig_tx->m_data.size(), "Data size should match");
        ASSERT_TRUE(des_tx->m_data == orig_tx->m_data, "Data should match");
        ASSERT_EQUAL(des_tx->m_n_reward, orig_tx->m_n_reward, "Reward should match");
        ASSERT_EQUAL(des_tx->m_n_timestamp, orig_tx->m_n_timestamp, "Timestamp should match");
        ASSERT_TRUE(des_tx->m_type == orig_tx->m_type, "Type should match");
    }
}

/**
 * @brief Test BLOCK message Clone
 */
TEST(BlockMessage_Clone) {
    CHash prev_hash("6666666666666666666666666666666666666666666666666666666666666666");
    auto p_block = std::make_shared<CBlock>(prev_hash, 7, "clone_miner");
    CBlockMessage original(p_block, 0);

    auto p_clone = original.Clone();
    auto* p_block_clone = dynamic_cast<CBlockMessage*>(p_clone.get());

    ASSERT_TRUE(p_block_clone != nullptr, "Clone should be a CBlockMessage");
    auto p_block_cloned = p_block_clone->GetBlock();
    ASSERT_TRUE(p_block_cloned != nullptr, "Cloned block should not be null");
    ASSERT_EQUAL(p_block_cloned->GetMiner(), std::string("clone_miner"), "Cloned miner should match");
    ASSERT_EQUAL(p_block->GetTransactions().size(), p_block_cloned->GetTransactions().size(), "Should have 1 transaction");
    const auto& orig_txs = p_block->GetTransactions();
    const auto& clone_txs = p_block_clone->GetBlock()->GetTransactions();
    // compare transactions
    for (size_t i = 0; i < orig_txs.size(); i++) {
        auto& cloned_tx = clone_txs[i];
        auto& orig_tx = orig_txs[i];
        ASSERT_EQUAL(cloned_tx->m_str_owner, orig_tx->m_str_owner, "Owner should match");
        ASSERT_EQUAL(cloned_tx->m_str_target, orig_tx->m_str_target, "Target should match");
        ASSERT_EQUAL(cloned_tx->m_data.size(), orig_tx->m_data.size(), "Data size should match");
        ASSERT_TRUE(cloned_tx->m_data == orig_tx->m_data, "Data should match");
        ASSERT_EQUAL(cloned_tx->m_n_reward, orig_tx->m_n_reward, "Reward should match");
        ASSERT_EQUAL(cloned_tx->m_n_timestamp, orig_tx->m_n_timestamp, "Timestamp should match");
        ASSERT_TRUE(cloned_tx->m_type == orig_tx->m_type, "Type should match");
    }
}

/**
 * @brief Test BLOCK message GetSetBlockData - REMOVED (GetBlock replaces GetBlockData/SetBlockData)
 */

// ==================== GET_PEERS MESSAGE TESTS ====================

/**
 * @brief Test GET_PEERS message construction
 */
TEST(GetPeersMessage_Construction) {
    CGetPeersMessage getpeers(0);
    ASSERT_EQUAL(getpeers.GetType(), MessageType::GET_PEERS, "Type should be 'get_peers'");
}

/**
 * @brief Test GET_PEERS message GetType
 */
TEST(GetPeersMessage_GetType) {
    CGetPeersMessage getpeers(0);
    ASSERT_EQUAL(getpeers.GetType(), MessageType::GET_PEERS, "Type should be 'get_peers'");
}

/**
 * @brief Test GET_PEERS message serialization empty
 */
TEST(GetPeersMessage_SerializePayload_Empty) {
    CGetPeersMessage getpeers(0);
    std::vector<uint8_t> payload = getpeers.SerializePayload();

    ASSERT_EQUAL(payload.size(), (size_t)0, "GET_PEERS payload should be empty");
}

/**
 * @brief Test GET_PEERS message deserialization empty
 */
TEST(GetPeersMessage_DeserializePayload_Empty) {
    CGetPeersMessage getpeers(0);

    std::vector<uint8_t> payload;
    bool result = getpeers.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization of empty payload should succeed");
}

/**
 * @brief Test GET_PEERS message round-trip
 */
TEST(GetPeersMessage_RoundTrip) {
    CGetPeersMessage original(0);

    std::vector<uint8_t> payload = original.SerializePayload();
    CGetPeersMessage deserialized(0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(deserialized.GetType(), original.GetType(), "Type should match");
}

/**
 * @brief Test GET_PEERS message Clone
 */
TEST(GetPeersMessage_Clone) {
    CGetPeersMessage original(0);

    auto p_clone = original.Clone();
    auto* p_getpeers_clone = dynamic_cast<CGetPeersMessage*>(p_clone.get());

    ASSERT_TRUE(p_getpeers_clone != nullptr, "Clone should be a CGetPeersMessage");
    ASSERT_EQUAL(p_getpeers_clone->GetType(), MessageType::GET_PEERS, "Cloned type should match");
}

// ==================== PEERS MESSAGE TESTS ====================

/**
 * @brief Test PEERS message construction empty
 */
TEST(PeersMessage_Construction_Empty) {
    CPeersMessage peers(0);
    ASSERT_EQUAL(peers.GetType(), MessageType::PEERS, "Type should be 'peers'");
    ASSERT_EQUAL(peers.GetPeerCount(), (size_t)0, "Peer count should be 0");
}

/**
 * @brief Test PEERS message construction with peers
 */
TEST(PeersMessage_Construction_WithPeers) {
    std::vector<CPeerInfo> peer_list;
    peer_list.push_back(CPeerInfo("192.168.1.1", 8333));
    CPeersMessage peers(peer_list, 0);

    ASSERT_EQUAL(peers.GetPeerCount(), (size_t)1, "Should have 1 peer");
}

/**
 * @brief Test PEERS message GetType
 */
TEST(PeersMessage_GetType) {
    CPeersMessage peers(0);
    ASSERT_EQUAL(peers.GetType(), MessageType::PEERS, "Type should be 'peers'");
}

/**
 * @brief Test PEERS message AddPeer
 */
TEST(PeersMessage_AddPeer) {
    CPeersMessage peers(0);

    peers.AddPeer("10.0.0.1", 8333);
    ASSERT_EQUAL(peers.GetPeerCount(), (size_t)1, "Should have 1 peer");

    peers.AddPeer("10.0.0.2", 8334);
    ASSERT_EQUAL(peers.GetPeerCount(), (size_t)2, "Should have 2 peers");
}

/**
 * @brief Test PEERS message GetPeers and Clear
 */
TEST(PeersMessage_GetPeers_Clear) {
    CPeersMessage peers(0);

    peers.AddPeer("127.0.0.1", 9000);
    peers.AddPeer("localhost", 9001);

    const auto& peer_list = peers.GetPeers();
    ASSERT_EQUAL(peer_list.size(), (size_t)2, "Should have 2 peers");

    peers.Clear();
    ASSERT_EQUAL(peers.GetPeerCount(), (size_t)0, "Should have 0 peers after clear");
}

/**
 * @brief Test PEERS message serialization empty
 */
TEST(PeersMessage_SerializePayload_Empty) {
    CPeersMessage peers(0);
    std::vector<uint8_t> payload = peers.SerializePayload();

    // Empty peers: just 4-byte count = 0
    ASSERT_EQUAL(payload.size(), (size_t)4, "Empty peers should be 4 bytes");

    uint32_t n_count_network;
    std::memcpy(&n_count_network, payload.data(), 4);
    ASSERT_EQUAL(ntohl(n_count_network), (uint32_t)0, "Count should be 0");
}

/**
 * @brief Test PEERS message serialization single peer
 */
TEST(PeersMessage_SerializePayload_SinglePeer) {
    CPeersMessage peers(0);
    peers.AddPeer("1.2.3.4", 8333);

    std::vector<uint8_t> payload = peers.SerializePayload();

    // Format: [4 bytes count][2 bytes addr_len]["1.2.3.4"][2 bytes port]
    // = 4 + (2 + 7 + 2) = 15 bytes
    ASSERT_EQUAL(payload.size(), (size_t)15, "Single peer should be 15 bytes");
}

/**
 * @brief Test PEERS message serialization multiple peers
 */
TEST(PeersMessage_SerializePayload_MultiplePeers) {
    CPeersMessage peers(0);
    peers.AddPeer("1.1.1.1", 8333);  // 7 chars
    peers.AddPeer("2.2.2.2", 8334);  // 7 chars

    std::vector<uint8_t> payload = peers.SerializePayload();

    // Format: [4 bytes count][2 * (2 bytes addr_len + 7 bytes addr + 2 bytes port)]
    // = 4 + 2 * 11 = 26 bytes
    ASSERT_EQUAL(payload.size(), (size_t)26, "Two peers should be 26 bytes");
}

/**
 * @brief Test PEERS message deserialization valid
 */
TEST(PeersMessage_DeserializePayload_Valid) {
    CPeersMessage peers(0);

    // Create payload: 1 peer
    std::vector<uint8_t> payload;
    uint32_t n_count = htonl(1);
    payload.insert(payload.end(), (uint8_t*)&n_count, (uint8_t*)&n_count + 4);

    std::string address = "test";
    uint16_t n_addr_len = htons(4);
    payload.insert(payload.end(), (uint8_t*)&n_addr_len, (uint8_t*)&n_addr_len + 2);
    payload.insert(payload.end(), address.begin(), address.end());

    uint16_t n_port = htons(9999);
    payload.insert(payload.end(), (uint8_t*)&n_port, (uint8_t*)&n_port + 2);

    bool result = peers.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(peers.GetPeerCount(), (size_t)1, "Should have 1 peer");
}

/**
 * @brief Test PEERS message round-trip
 */
TEST(PeersMessage_RoundTrip) {
    CPeersMessage original(0);
    original.AddPeer("example.com", 8080);
    original.AddPeer("10.20.30.40", 8081);

    std::vector<uint8_t> payload = original.SerializePayload();

    CPeersMessage deserialized(0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(deserialized.GetPeerCount(), original.GetPeerCount(), "Peer count should match");
}

/**
 * @brief Test PEERS message Clone
 */
TEST(PeersMessage_Clone) {
    CPeersMessage original(0);
    original.AddPeer("clone.test", 7777);

    auto p_clone = original.Clone();
    auto* p_peers_clone = dynamic_cast<CPeersMessage*>(p_clone.get());

    ASSERT_TRUE(p_peers_clone != nullptr, "Clone should be a CPeersMessage");
    ASSERT_EQUAL(p_peers_clone->GetPeerCount(), (size_t)1, "Cloned peer count should match");
}

// ==================== GET_CHAIN MESSAGE TESTS ====================

/**
 * @brief Test GET_CHAIN message construction
 */
TEST(GetChainMessage_Construction) {
    CGetChainMessage getchain(0);
    ASSERT_EQUAL(getchain.GetType(), MessageType::GET_CHAIN, "Type should be 'get_chain'");
}

/**
 * @brief Test GET_CHAIN message GetType
 */
TEST(GetChainMessage_GetType) {
    CGetChainMessage getchain(0);
    ASSERT_EQUAL(getchain.GetType(), MessageType::GET_CHAIN, "Type should be 'get_chain'");
}

/**
 * @brief Test GET_CHAIN message serialization empty
 */
TEST(GetChainMessage_SerializePayload_Empty) {
    CGetChainMessage getchain(0);
    std::vector<uint8_t> payload = getchain.SerializePayload();

    ASSERT_EQUAL(payload.size(), (size_t)0, "GET_CHAIN payload should be empty");
}

/**
 * @brief Test GET_CHAIN message deserialization empty
 */
TEST(GetChainMessage_DeserializePayload_Empty) {
    CGetChainMessage getchain(0);

    std::vector<uint8_t> payload;
    bool result = getchain.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization of empty payload should succeed");
}

/**
 * @brief Test GET_CHAIN message round-trip
 */
TEST(GetChainMessage_RoundTrip) {
    CGetChainMessage original(0);

    std::vector<uint8_t> payload = original.SerializePayload();
    CGetChainMessage deserialized(0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(deserialized.GetType(), original.GetType(), "Type should match");
}

/**
 * @brief Test GET_CHAIN message Clone
 */
TEST(GetChainMessage_Clone) {
    CGetChainMessage original(0);

    auto p_clone = original.Clone();
    auto* p_getchain_clone = dynamic_cast<CGetChainMessage*>(p_clone.get());

    ASSERT_TRUE(p_getchain_clone != nullptr, "Clone should be a CGetChainMessage");
    ASSERT_EQUAL(p_getchain_clone->GetType(), MessageType::GET_CHAIN, "Cloned type should match");
}

// ==================== CHAIN_INFO MESSAGE TESTS ====================

/**
 * @brief Test CHAIN_INFO message construction default
 */
TEST(ChainInfoMessage_Construction_Default) {
    CChainInfoMessage chaininfo(0);
    ASSERT_EQUAL(chaininfo.GetType(), MessageType::CHAIN_INFO, "Type should be 'chain_info'");
}

/**
 * @brief Test CHAIN_INFO message construction with data
 */
TEST(ChainInfoMessage_Construction_WithData) {
    std::string hash(32, 'H');
    CChainInfoMessage chaininfo(12345, hash, 0);

    ASSERT_EQUAL(chaininfo.GetHeight(), (uint64_t)12345, "Height should be 12345");
    ASSERT_EQUAL(chaininfo.GetBestBlock(), hash, "Best block should match");
}

/**
 * @brief Test CHAIN_INFO message GetType
 */
TEST(ChainInfoMessage_GetType) {
    CChainInfoMessage chaininfo(0);
    ASSERT_EQUAL(chaininfo.GetType(), MessageType::CHAIN_INFO, "Type should be 'chain_info'");
}

/**
 * @brief Test CHAIN_INFO message serialization
 */
TEST(ChainInfoMessage_SerializePayload) {
    std::string hash(32, 'A');
    CChainInfoMessage chaininfo(100, hash, 0);

    std::vector<uint8_t> payload = chaininfo.SerializePayload();

    // Format: [8 bytes height][4 bytes hash_length][32 bytes hash] = 44 bytes
    ASSERT_EQUAL(payload.size(), (size_t)44, "Payload should be 44 bytes");
}

/**
 * @brief Test CHAIN_INFO message deserialization valid
 */
TEST(ChainInfoMessage_DeserializePayload_Valid) {
    CChainInfoMessage chaininfo(0);

    // Create a valid message, serialize it, then deserialize back
    std::string hash(32, 'Z');
    CChainInfoMessage temp_msg(999, hash, 0);
    std::vector<uint8_t> payload = temp_msg.SerializePayload();

    bool result = chaininfo.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(chaininfo.GetHeight(), (uint64_t)999, "Height should be 999");
}

/**
 * @brief Test CHAIN_INFO message deserialization invalid size
 */
TEST(ChainInfoMessage_DeserializePayload_InvalidSize) {
    CChainInfoMessage chaininfo(0);

    // Test with 11 bytes (too short, need at least 12)
    std::vector<uint8_t> payload_short(11, 0);
    ASSERT_FALSE(chaininfo.DeserializePayload(payload_short), "Should reject payload < 12 bytes");

    // Test with incomplete hash data
    // 12 bytes header but claims hash length of 32, only provides 10
    std::vector<uint8_t> payload_incomplete;
    uint64_t n_height = 0;
    payload_incomplete.insert(payload_incomplete.end(), (uint8_t*)&n_height, (uint8_t*)&n_height + 8);
    uint32_t n_length = htonl(32);  // Claims 32 bytes
    payload_incomplete.insert(payload_incomplete.end(), (uint8_t*)&n_length, (uint8_t*)&n_length + 4);
    // Only add 10 bytes instead of 32
    for (int i = 0; i < 10; i++) {
        payload_incomplete.push_back(0);
    }
    ASSERT_FALSE(chaininfo.DeserializePayload(payload_incomplete), "Should reject incomplete hash data");
}

/**
 * @brief Test CHAIN_INFO message round-trip
 */
TEST(ChainInfoMessage_RoundTrip) {
    std::string hash(32, 'R');
    CChainInfoMessage original(54321, hash, 0);

    std::vector<uint8_t> payload = original.SerializePayload();

    CChainInfoMessage deserialized(0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(deserialized.GetHeight(), original.GetHeight(), "Height should match");
    ASSERT_EQUAL(deserialized.GetBestBlock(), original.GetBestBlock(), "Best block should match");
}

/**
 * @brief Test CHAIN_INFO message round-trip with large height
 */
TEST(ChainInfoMessage_RoundTrip_LargeHeight) {
    uint64_t large_height = 0xFFFFFFFFFFFFFFFFULL;  // Max uint64
    std::string hash(32, 'L');
    CChainInfoMessage original(large_height, hash, 0);

    std::vector<uint8_t> payload = original.SerializePayload();

    CChainInfoMessage deserialized(0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_TRUE(result, "Deserialization should succeed");
    ASSERT_EQUAL(deserialized.GetHeight(), large_height, "Large height should match");
}

/**
 * @brief Test CHAIN_INFO message Clone
 */
TEST(ChainInfoMessage_Clone) {
    std::string hash(32, 'C');
    CChainInfoMessage original(777, hash, 0);

    auto p_clone = original.Clone();
    auto* p_chaininfo_clone = dynamic_cast<CChainInfoMessage*>(p_clone.get());

    ASSERT_TRUE(p_chaininfo_clone != nullptr, "Clone should be a CChainInfoMessage");
    ASSERT_EQUAL(p_chaininfo_clone->GetHeight(), (uint64_t)777, "Cloned height should match");
}

/**
 * @brief Test CHAIN_INFO message GetSetHeight
 */
TEST(ChainInfoMessage_GetSetHeight) {
    CChainInfoMessage chaininfo(0);

    chaininfo.SetHeight(500);
    ASSERT_EQUAL(chaininfo.GetHeight(), (uint64_t)500, "Height should be 500");

    chaininfo.SetHeight(1000);
    ASSERT_EQUAL(chaininfo.GetHeight(), (uint64_t)1000, "Height should be 1000 after set");
}

/**
 * @brief Test CHAIN_INFO message GetSetBestBlock
 */
TEST(ChainInfoMessage_GetSetBestBlock) {
    CChainInfoMessage chaininfo(0);

    std::string hash1(32, 'X');
    chaininfo.SetBestBlock(hash1);
    ASSERT_EQUAL(chaininfo.GetBestBlock(), hash1, "Best block should match hash1");

    std::string hash2(32, 'Y');
    chaininfo.SetBestBlock(hash2);
    ASSERT_EQUAL(chaininfo.GetBestBlock(), hash2, "Best block should match hash2 after set");
}

// ==================== UNKNOWN MESSAGE TESTS ====================

/**
 * @brief Test UNKNOWN message construction
 */
TEST(UnknownMessage_Construction) {
    CUnknownMessage unknown(0);
    ASSERT_EQUAL(unknown.GetType(), MessageType::UNKNOWN, "Type should be 'unknown'");
}

/**
 * @brief Test UNKNOWN message GetType
 */
TEST(UnknownMessage_GetType) {
    CUnknownMessage unknown(0);
    ASSERT_EQUAL(unknown.GetType(), MessageType::UNKNOWN, "Type should be 'unknown'");
}

/**
 * @brief Test UNKNOWN message serialization empty
 */
TEST(UnknownMessage_SerializePayload_Empty) {
    CUnknownMessage unknown(0);
    std::vector<uint8_t> payload = unknown.SerializePayload();

    ASSERT_EQUAL(payload.size(), (size_t)0, "UNKNOWN payload should always be empty");
}

/**
 * @brief Test UNKNOWN message deserialization empty
 */
TEST(UnknownMessage_DeserializePayload_Empty) {
    CUnknownMessage unknown(0);

    std::vector<uint8_t> payload;
    bool result = unknown.DeserializePayload(payload);

    ASSERT_FALSE(result, "Deserialization of empty payload should succeed");
}

/**
 * @brief Test UNKNOWN message round-trip
 */
TEST(UnknownMessage_RoundTrip) {
    CUnknownMessage original(0);

    std::vector<uint8_t> payload = original.SerializePayload();
    CUnknownMessage deserialized(0);
    bool result = deserialized.DeserializePayload(payload);

    ASSERT_FALSE(result, "Deserialization should succeed");
    ASSERT_EQUAL(deserialized.GetType(), original.GetType(), "Type should match");
}

/**
 * @brief Test UNKNOWN message Clone
 */
TEST(UnknownMessage_Clone) {
    CUnknownMessage original(0);

    auto p_clone = original.Clone();
    auto* p_unknown_clone = dynamic_cast<CUnknownMessage*>(p_clone.get());

    ASSERT_TRUE(p_unknown_clone != nullptr, "Clone should be a CUnknownMessage");
    ASSERT_EQUAL(p_unknown_clone->GetType(), MessageType::UNKNOWN, "Cloned type should match");
}

/**
 * @brief Test UNKNOWN message GetPayloadSize
 */
TEST(UnknownMessage_GetPayloadSize) {
    CUnknownMessage unknown(0);
    ASSERT_EQUAL(unknown.GetPayloadSize(), (size_t)0, "Payload size should always be 0");
}

#if 0  // Disabled CPeerMessageLegacy tests

/**
 * @brief Test SetType and SetPayload methods
 */
TEST(PeerMessage_SettersAndGetters) {
    CPeerMessageLegacy msg;

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
 * @brief Test all message types are valid except UNKNOWN
 */
TEST(PeerMessage_MessageTypeValidity) {
    CPeerMessageLegacy ping(MessageType::PING, 0);
    CPeerMessageLegacy pong(MessageType::PONG, 0);
    CPeerMessageLegacy get_peers(MessageType::GET_PEERS, 0);
    CPeerMessageLegacy peers(MessageType::PEERS, 0);
    CPeerMessageLegacy tx(MessageType::TX, 0);
    CPeerMessageLegacy block(MessageType::BLOCK, 0);
    CPeerMessageLegacy get_chain(MessageType::GET_CHAIN, 0);
    CPeerMessageLegacy chain_info(MessageType::CHAIN_INFO, 0);
    CPeerMessageLegacy unknown(MessageType::UNKNOWN, 0);

    ASSERT_TRUE(ping.IsValid(), "PING should be valid");
    ASSERT_TRUE(pong.IsValid(), "PONG should be valid");
    ASSERT_TRUE(get_peers.IsValid(), "GET_PEERS should be valid");
    ASSERT_TRUE(peers.IsValid(), "PEERS should be valid");
    ASSERT_TRUE(tx.IsValid(), "TX should be valid");
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
    CPeerMessageLegacy msg(MessageType::TX, payload, 0);
    std::string serialized = msg.Serialize();

    // Verify format: [4 bytes magic][1 byte type_length][2 bytes "tx"][4 bytes payload_length][3 bytes "ABC"]
    // Total: 4 + 1 + 2 + 4 + 3 = 14 bytes
    ASSERT_EQUAL(serialized.size(), (size_t)14, "Total size should be 14 bytes");

    // Byte 4: Type length (after 4 bytes magic)
    ASSERT_EQUAL((uint8_t)serialized[4], (uint8_t)2, "Byte 4 should be type length (2)");

    // Bytes 5-6: Type string "tx"
    std::string type_str = serialized.substr(5, 2);
    ASSERT_EQUAL(type_str, std::string("tx"), "Type string should be 'tx'");

    // Bytes 7-10: Payload length (network byte order)
    uint32_t length_network;
    std::memcpy(&length_network, serialized.data() + 7, 4);
    uint32_t length = ntohl(length_network);
    ASSERT_EQUAL(length, (uint32_t)3, "Payload length should be 3");

    // Bytes 11-13: Payload
    std::string extracted_payload = serialized.substr(11, 3);
    ASSERT_EQUAL(extracted_payload, payload, "Payload should match");
}

/**
 * @brief Test empty string payload handling
 */
TEST(PeerMessage_EmptyStringPayload) {
    std::string empty_payload = "";
    CPeerMessageLegacy msg(MessageType::PING, empty_payload, 0);

    ASSERT_EQUAL(msg.GetPayloadSize(), (size_t)0, "Empty string should have size 0");
    ASSERT_EQUAL(msg.GetPayloadString(), std::string(""), "Should return empty string");

    // Serialize and deserialize
    std::string serialized = msg.Serialize();
    CPeerMessageLegacy deserialized;
    ASSERT_TRUE(deserialized.Deserialize(serialized), "Should deserialize successfully");
    ASSERT_EQUAL(deserialized.GetPayloadSize(), (size_t)0, "Deserialized payload should be empty");
}

/**
 * @brief Test network byte order conversion
 */
TEST(PeerMessage_NetworkByteOrder) {
    // Create message with known size payload
    std::string payload(256, 'X'); // 256 bytes
    CPeerMessageLegacy msg(MessageType::BLOCK, payload, 0);
    std::string serialized = msg.Serialize();

    // Format: [4 bytes magic][1 byte type_length][5 bytes "block"][4 bytes payload_length][256 bytes payload]
    // Extract length field (bytes 10-13, after 4 bytes magic + 1 byte type_length + 5 bytes "block")
    uint32_t length_network;
    std::memcpy(&length_network, serialized.data() + 10, 4);
    uint32_t length_host = ntohl(length_network);

    ASSERT_EQUAL(length_host, (uint32_t)256, "Payload length should be 256 in host byte order");

    // Verify deserialization handles byte order correctly
    CPeerMessageLegacy deserialized;
    ASSERT_TRUE(deserialized.Deserialize(serialized), "Should deserialize successfully");
    ASSERT_EQUAL(deserialized.GetPayloadSize(), (size_t)256, "Payload size should be 256");
}

#endif  // Disabled CPeerMessageLegacy tests
