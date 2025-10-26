// ============= test_peer_manager.cpp =============
#include "unit_test.h"
#include "peer/peer_manager.h"
#include "peer/peer_message.h"
#include <thread>
#include <chrono>
#include <atomic>

using namespace UnitTest;

/**
 * @brief Test CPeerConnection default constructor
 */
TEST(PeerConnection_DefaultConstructor) {
    CPeerConnection peer;

    ASSERT_EQUAL(peer.n_socket, -1, "Default socket should be -1");
    ASSERT_TRUE(peer.str_address.empty(), "Default address should be empty");
    ASSERT_EQUAL(peer.n_port, 0, "Default port should be 0");
    ASSERT_FALSE(peer.f_connected, "Default connection status should be false");
    ASSERT_FALSE(peer.f_active, "Default active status should be false");
}

/**
 * @brief Test CPeerConnection parameterized constructor
 */
TEST(PeerConnection_ParameterizedConstructor) {
    CPeerConnection peer("127.0.0.1", 8333);

    ASSERT_EQUAL(peer.str_address, std::string("127.0.0.1"), "Address should be set");
    ASSERT_EQUAL(peer.n_port, 8333, "Port should be set");
    ASSERT_EQUAL(peer.n_socket, -1, "Socket should initially be -1");
    ASSERT_FALSE(peer.f_connected, "Should not be connected initially");
    ASSERT_FALSE(peer.f_active, "Should not be active initially");
}

/**
 * @brief Test CPeerConnection move constructor
 */
TEST(PeerConnection_MoveConstructor) {
    CPeerConnection peer1("192.168.1.1", 9999);
    peer1.f_connected = true;
    peer1.f_active = true;

    CPeerConnection peer2(std::move(peer1));

    ASSERT_EQUAL(peer2.str_address, std::string("192.168.1.1"), "Address should be moved");
    ASSERT_EQUAL(peer2.n_port, 9999, "Port should be moved");
    ASSERT_TRUE(peer2.f_connected, "Connection status should be moved");
    ASSERT_TRUE(peer2.f_active, "Active status should be moved");
}

/**
 * @brief Test CPeerManager constructor
 */
TEST(PeerManager_Constructor) {
    CPeerManager manager(8333);

    ASSERT_FALSE(manager.IsRunning(), "Manager should not be running initially");
    ASSERT_EQUAL(manager.GetOutboundPeerCount(), (size_t)0, "Should have no outbound peers initially");
    ASSERT_EQUAL(manager.GetInboundPeerCount(), (size_t)0, "Should have no inbound peers initially");
}

/**
 * @brief Test CPeerManager GetConnectedPeers on empty peer list
 */
TEST(PeerManager_GetConnectedPeers_Empty) {
    CPeerManager manager(8334);

    auto peers = manager.GetConnectedPeers();
    ASSERT_EQUAL(peers.size(), (size_t)0, "Should have no connected peers");
}

/**
 * @brief Test thread-safe peer count access
 *
 * Tests that multiple threads can safely query peer count concurrently.
 * Multiple threads can safely read the outbound peer count simultaneously
 * No data races occur when accessing internal peer data structures
 * The method doesn't deadlock when called from multiple threads
 * The peer manager's mutex/locking mechanism works correctly
 *
 * The test demonstrates that GetOutboundPeerCount() is safe to call from
 * multiple threads in a real-world scenario where many components might
 * query peer status concurrently.
 */
TEST(PeerManager_ThreadSafe_GetOutboundPeerCount) {
    CPeerManager manager(8335);

    std::atomic<int> query_count{0};
    std::atomic<bool> stop_flag{false};

    // Start multiple threads querying peer count
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&manager, &query_count, &stop_flag]() {
            while (!stop_flag) {
                size_t count = manager.GetOutboundPeerCount();
                ASSERT_EQUAL(count, 0, "Outbound peer should be 0");
                query_count++;
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }

    // Let threads run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop_flag = true;

    // Join all threads
    for (auto& t : threads) {
        t.join();
    }

    // Verify we made many queries without crashing
    ASSERT_TRUE(query_count > 100, "Should have made multiple queries");
}

/**
 * @brief Test thread-safe GetConnectedPeers access
 *
 * Tests that multiple threads can safely query connected peers concurrently.
 */
TEST(PeerManager_ThreadSafe_GetConnectedPeers) {
    CPeerManager manager(8336);

    std::atomic<int> query_count{0};
    std::atomic<bool> stop_flag{false};

    // Start multiple threads querying connected peers
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&manager, &query_count, &stop_flag]() {
            while (!stop_flag) {
                auto peers = manager.GetConnectedPeers();
                query_count++;
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }

    // Let threads run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop_flag = true;

    // Join all threads
    for (auto& t : threads) {
        t.join();
    }

    // Verify we made many queries without crashing
    ASSERT_TRUE(query_count > 100, "Should have made multiple queries");
}

/**
 * @brief Test BroadcastMessage with empty payload
 */
TEST(PeerManager_BroadcastMessage_EmptyPayload) {
    CPeerManager manager(8337);

    // Create TX_IDS message with empty payload
    CPeerMessage tx_ids_msg(EMessageType::TX_IDS, "");

    // Should not crash with empty payload
    size_t sent_count = manager.BroadcastMessage(tx_ids_msg);

    ASSERT_EQUAL(sent_count, (size_t)0, "Should send to 0 peers when none connected");
}

/**
 * @brief Test BroadcastMessage with TX_IDS payload
 */
TEST(PeerManager_BroadcastMessage_TxIdsPayload) {
    CPeerManager manager(8338);

    // Create TX_IDS message with comma-separated transaction IDs
    std::string tx_ids_payload = "tx1_abcdef1234567890,tx2_1234567890abcdef";
    CPeerMessage tx_ids_msg(EMessageType::TX_IDS, tx_ids_payload);

    // Should not crash with no peers
    size_t sent_count = manager.BroadcastMessage(tx_ids_msg);

    ASSERT_EQUAL(sent_count, (size_t)0, "Should send to 0 peers when none connected");
}

/**
 * @brief Test atomic f_active flag
 *
 * Verifies that the f_active flag is properly atomic and can be
 * safely accessed from multiple threads.
 */
TEST(PeerConnection_AtomicFlag) {
    CPeerConnection peer("127.0.0.1", 8080);

    std::atomic<int> read_count{0};
    std::atomic<bool> stop_flag{false};

    // Thread 1: Toggles f_active
    std::thread writer([&peer, &stop_flag]() {
        bool value = false;
        while (!stop_flag) {
            peer.f_active = value;
            value = !value;
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    });

    // Thread 2-5: Read f_active
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; i++) {
        readers.emplace_back([&peer, &read_count, &stop_flag]() {
            while (!stop_flag) {
                bool value = peer.f_active;
                (void)value; // Suppress unused warning
                read_count++;
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });
    }

    // Let threads run
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop_flag = true;

    // Join threads
    writer.join();
    for (auto& t : readers) {
        t.join();
    }

    // Verify we made many safe accesses
    ASSERT_TRUE(read_count > 1000, "Should have made many atomic reads");
}

/**
 * @brief Test GetInboundPeerCount
 *
 * Verifies that GetInboundPeerCount() returns correct count
 * and is separate from outbound peer count.
 */
TEST(PeerManager_GetInboundPeerCount) {
    CPeerManager manager(8339);

    ASSERT_EQUAL(manager.GetInboundPeerCount(), (size_t)0, "Should start with 0 inbound peers");
    ASSERT_EQUAL(manager.GetOutboundPeerCount(), (size_t)0, "Should start with 0 outbound peers");
}

/**
 * @brief Test thread-safe inbound peer count access
 *
 * Tests that multiple threads can safely query inbound peer count concurrently.
 * Verifies thread safety of GetInboundPeerCount() method.
 */
TEST(PeerManager_ThreadSafe_GetInboundPeerCount) {
    CPeerManager manager(8340);

    std::atomic<int> query_count{0};
    std::atomic<bool> stop_flag{false};

    // Start multiple threads querying inbound peer count
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&manager, &query_count, &stop_flag]() {
            while (!stop_flag) {
                size_t count = manager.GetInboundPeerCount();
                ASSERT_EQUAL(count, 0, "Inbound peer count should be 0");
                query_count++;
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }

    // Let threads run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop_flag = true;

    // Join all threads
    for (auto& t : threads) {
        t.join();
    }

    // Verify we made many queries without crashing
    ASSERT_TRUE(query_count > 100, "Should have made multiple queries");
}

/**
 * @brief Test CPeerManager constructor with custom peer limits
 *
 * Verifies that peer manager can be constructed with custom
 * inbound and outbound peer limits.
 */
TEST(PeerManager_ConstructorWithLimits) {
    // Create manager with 4 outbound and 10 inbound peers max
    CPeerManager manager(8341, 4, 10);

    ASSERT_FALSE(manager.IsRunning(), "Manager should not be running initially");
    ASSERT_EQUAL(manager.GetOutboundPeerCount(), (size_t)0, "Should have no outbound peers initially");
    ASSERT_EQUAL(manager.GetInboundPeerCount(), (size_t)0, "Should have no inbound peers initially");
}

/**
 * @brief Test that inbound and outbound peer counts are independent
 *
 * Verifies that inbound and outbound peer tracking is completely separate.
 */
TEST(PeerManager_InboundOutboundIndependent) {
    CPeerManager manager(8342);

    // Both should start at 0
    ASSERT_EQUAL(manager.GetInboundPeerCount(), (size_t)0, "Inbound should be 0");
    ASSERT_EQUAL(manager.GetOutboundPeerCount(), (size_t)0, "Outbound should be 0");

    // GetConnectedPeers should return empty for no peers
    auto peers = manager.GetConnectedPeers();
    ASSERT_EQUAL(peers.size(), (size_t)0, "Should have no connected peers");
}

/**
 * @brief Test concurrent access to both inbound and outbound peer counts
 *
 * Tests that multiple threads can safely query both inbound and outbound
 * peer counts concurrently without race conditions.
 */
TEST(PeerManager_ThreadSafe_BothPeerCounts) {
    CPeerManager manager(8343);

    std::atomic<int> inbound_queries{0};
    std::atomic<int> outbound_queries{0};
    std::atomic<bool> stop_flag{false};

    // Threads querying inbound count
    std::vector<std::thread> inbound_threads;
    for (int i = 0; i < 5; i++) {
        inbound_threads.emplace_back([&manager, &inbound_queries, &stop_flag]() {
            while (!stop_flag) {
                size_t count = manager.GetInboundPeerCount();
                ASSERT_EQUAL(count, 0, "Inbound count should be 0");
                inbound_queries++;
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }

    // Threads querying outbound count
    std::vector<std::thread> outbound_threads;
    for (int i = 0; i < 5; i++) {
        outbound_threads.emplace_back([&manager, &outbound_queries, &stop_flag]() {
            while (!stop_flag) {
                size_t count = manager.GetOutboundPeerCount();
                ASSERT_EQUAL(count, 0, "Outbound count should be 0");
                outbound_queries++;
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }

    // Let threads run
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop_flag = true;

    // Join all threads
    for (auto& t : inbound_threads) {
        t.join();
    }
    for (auto& t : outbound_threads) {
        t.join();
    }

    // Verify we made many queries without crashing
    ASSERT_TRUE(inbound_queries > 100, "Should have made multiple inbound queries");
    ASSERT_TRUE(outbound_queries > 100, "Should have made multiple outbound queries");
}

/**
 * @brief Test SendMessageToPeer with no connected peers
 */
TEST(PeerManager_SendMessageToPeer_NoPeers) {
    CPeerManager manager(8344);

    // Create a PING message
    CPeerMessage ping_msg(EMessageType::PING);

    // Try to send to non-existent peer
    bool result = manager.SendMessageToPeer("192.168.1.100", 8333, ping_msg);

    ASSERT_FALSE(result, "Should fail when peer is not connected");
}

/**
 * @brief Test BroadcastMessage with no connected peers
 */
TEST(PeerManager_BroadcastMessage_NoPeers) {
    CPeerManager manager(8345);

    // Create a GET_PEERS message
    CPeerMessage get_peers_msg(EMessageType::GET_PEERS);

    // Broadcast to no peers
    size_t sent_count = manager.BroadcastMessage(get_peers_msg);

    ASSERT_EQUAL(sent_count, (size_t)0, "Should send to 0 peers when none connected");
}

/**
 * @brief Test BroadcastMessage with different message types
 */
TEST(PeerManager_BroadcastMessage_DifferentTypes) {
    CPeerManager manager(8346);

    // Test different message types
    CPeerMessage ping(EMessageType::PING);
    CPeerMessage pong(EMessageType::PONG);
    CPeerMessage get_peers(EMessageType::GET_PEERS);
    CPeerMessage tx_ids(EMessageType::TX_IDS, "tx1,tx2,tx3");

    // All should return 0 with no peers
    ASSERT_EQUAL(manager.BroadcastMessage(ping), (size_t)0, "PING should send to 0 peers");
    ASSERT_EQUAL(manager.BroadcastMessage(pong), (size_t)0, "PONG should send to 0 peers");
    ASSERT_EQUAL(manager.BroadcastMessage(get_peers), (size_t)0, "GET_PEERS should send to 0 peers");
    ASSERT_EQUAL(manager.BroadcastMessage(tx_ids), (size_t)0, "TX_IDS should send to 0 peers");
}

