// ============= test_peer.cpp =============
#include "unit_test.h"
#include "peer/peer.h"
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
    ASSERT_EQUAL(manager.GetOutboundPeerCount(), (size_t)0, "Should have no peers initially");
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
 * @brief Test BroadcastTransactionIds with empty list
 */
TEST(PeerManager_BroadcastTransactionIds_Empty) {
    CPeerManager manager(8337);

    std::vector<std::string> empty_ids;

    // Should not crash with empty list
    manager.BroadcastTransactionIds(empty_ids);

    ASSERT_TRUE(true, "Should handle empty transaction list");
}

/**
 * @brief Test BroadcastTransactionIds with no connected peers
 */
TEST(PeerManager_BroadcastTransactionIds_NoPeers) {
    CPeerManager manager(8338);

    std::vector<std::string> tx_ids = {
        "tx1_abcdef1234567890",
        "tx2_1234567890abcdef"
    };

    // Should not crash with no peers
    manager.BroadcastTransactionIds(tx_ids);

    ASSERT_TRUE(true, "Should handle broadcast with no peers");
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

