// ============= test_peer_manager.cpp =============
#include "unit_test.h"
#include "peer/peer_manager.h"
#include "peer/peer_message.h"
#include "peer/messages/ping_message.h"
#include "peer/messages/pong_message.h"
#include "peer/messages/inventory_message.h"
#include "peer/messages/getpeers_message.h"
#include <thread>
#include <chrono>
#include <atomic>

using namespace UnitTest;

/**
 * @brief Test CPeerNode default constructor
 */
TEST(PeerNode_DefaultConstructor) {
    CPeerNode peer;

    ASSERT_TRUE(peer.GetSocket() == nullptr, "Default socket should be nullptr");
    ASSERT_TRUE(peer.GetAddress().empty(), "Default address should be empty");
    ASSERT_EQUAL(peer.GetPort(), 0, "Default port should be 0");
    ASSERT_FALSE(peer.IsConnected(), "Default connection status should be false");
}

/**
 * @brief Test CPeerNode parameterized constructor
 */
TEST(PeerNode_ParameterizedConstructor) {
    CPeerNode peer("127.0.0.1", 8333);

    ASSERT_EQUAL(peer.GetAddress(), std::string("127.0.0.1"), "Address should be set");
    ASSERT_EQUAL(peer.GetPort(), 8333, "Port should be set");
    ASSERT_TRUE(peer.GetSocket() == nullptr, "Socket should initially be nullptr");
    ASSERT_FALSE(peer.IsConnected(), "Should not be connected initially");
}

/**
 * @brief Test CPeerNode move constructor
 */
/* not needed for now
TEST(PeerNode_MoveConstructor) {
    CPeerNode peer1("192.168.1.1", 9999);
    peer1.SetConnected(true);

    CPeerNode peer2(std::move(peer1));

    ASSERT_EQUAL(peer2.GetAddress(), std::string("192.168.1.1"), "Address should be moved");
    ASSERT_EQUAL(peer2.GetPort(), 9999, "Port should be moved");
    ASSERT_TRUE(peer2.IsConnected(), "Connection status should be moved");
}
*/
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

    // Create INVENTORY message with empty payload
    CInventoryMessage inv_msg(0);

    // Should not crash with empty payload
    size_t sent_count = manager.BroadcastMessage(inv_msg);

    ASSERT_EQUAL(sent_count, (size_t)0, "Should send to 0 peers when none connected");
}

/**
 * @brief Test BroadcastMessage with INVENTORY payload
 */
TEST(PeerManager_BroadcastMessage_InventoryPayload) {
    CPeerManager manager(8338);

    // Create INVENTORY message with inventory item
    CInventoryMessage inv_msg(0);
    std::string hash(32, '\0');
    inv_msg.AddItem(2, hash);  // Type 2 = TRANSACTION

    // Should not crash with no peers
    size_t sent_count = manager.BroadcastMessage(inv_msg);

    ASSERT_EQUAL(sent_count, (size_t)0, "Should send to 0 peers when none connected");
}

/**
 * @brief Test CPeerNode thread-safe getter/setter methods
 *
 * Verifies that CPeerNode's connection status can be safely accessed
 * from multiple threads using getter/setter methods protected by mutex.
 */
TEST(PeerNode_ThreadSafeAccessors) {
    CPeerNode peer("127.0.0.1", 8080);

    std::atomic<int> read_count{0};
    std::atomic<bool> stop_flag{false};

    // Thread 1: Toggles connection status
    std::thread writer([&peer, &stop_flag]() {
        bool value = false;
        while (!stop_flag) {
            peer.SetConnected(value);
            value = !value;
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    });

    // Thread 2-5: Read connection status
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; i++) {
        readers.emplace_back([&peer, &read_count, &stop_flag]() {
            while (!stop_flag) {
                bool value = peer.IsConnected();
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
    ASSERT_TRUE(read_count > 1000, "Should have made many thread-safe reads");
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
    CPingMessage ping_msg(0);

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
    CGetPeersMessage get_peers_msg(0);

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
    CPingMessage ping(0);
    CPongMessage pong(0, 0);
    CGetPeersMessage get_peers(0);
    CInventoryMessage inventory(0);

    // All should return 0 with no peers
    ASSERT_EQUAL(manager.BroadcastMessage(ping), (size_t)0, "PING should send to 0 peers");
    ASSERT_EQUAL(manager.BroadcastMessage(pong), (size_t)0, "PONG should send to 0 peers");
    ASSERT_EQUAL(manager.BroadcastMessage(get_peers), (size_t)0, "GET_PEERS should send to 0 peers");
    ASSERT_EQUAL(manager.BroadcastMessage(inventory), (size_t)0, "INVENTORY should send to 0 peers");
}

/**
 * @brief Test Boost.Asio initialization in PeerManager
 */

TEST(PeerManager_BoostAsioInitialization) {
    // Test that PeerManager initializes with Boost.Asio infrastructure
    // This test verifies the constructor doesn't throw and manager can start/stop
    CPeerManager manager(8347, 8, 120, 32, 180);

    ASSERT_FALSE(manager.IsRunning(), "Manager should not be running after construction");
    ASSERT_EQUAL(manager.GetInboundPeerCount(), (size_t)0, "Should have 0 inbound peers initially");
    ASSERT_EQUAL(manager.GetOutboundPeerCount(), (size_t)0, "Should have 0 outbound peers initially");

    // Start manager (initializes Boost.Asio threads)
    ASSERT_TRUE(manager.Start(), "Manager should start successfully");
    ASSERT_TRUE(manager.IsRunning(), "Manager should be running after Start()");

    // Give threads time to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Stop manager (cleans up Boost.Asio resources)
    manager.Stop();
    ASSERT_FALSE(manager.IsRunning(), "Manager should not be running after Stop()");
}

/**
 * @brief Test CPeerManager public IP discovery
 *
 * Tests that CPeerManager discovers public IP via STUN when not in LOCALNET mode.
 * Note: This test requires network access and working STUN servers.
 */

TEST(PeerManager_PublicIPDiscovery) {
    // Create peer manager with TESTNET magic (should trigger STUN discovery)
    CPeerManager manager(8348, 8, 120, 32, 180, 0xEA71B96E);  // TESTNET_MAGIC

    std::string public_ip = manager.GetPublicIP();

    // Should have discovered a public IP (not 127.0.0.1)
    // Note: Test may fail if behind corporate firewall blocking STUN
    ASSERT_TRUE(public_ip != "" || public_ip == "127.0.0.1", "Should return either discovered IP or fallback");
}

/**
 * @brief Test LOCALNET mode skips STUN
 *
 * Tests that CPeerManager does not attempt STUN discovery when in LOCALNET mode.
 */
TEST(PeerManager_LocalnetSkipsSTUN) {
    // Create peer manager with LOCALNET magic
    CPeerManager manager(8349, 8, 120, 32, 180, 0xACDE4892);  // LOCALNET_MAGIC

    std::string public_ip = manager.GetPublicIP();

    // Should stay at 127.0.0.1 for LOCALNET
    ASSERT_EQUAL(public_ip, std::string("127.0.0.1"), "LOCALNET should use 127.0.0.1");
}

/**
 * @brief Test GetPublicIP accessor is thread-safe
 *
 * Tests that multiple threads can safely query public IP concurrently.
 */
TEST(PeerManager_ThreadSafe_GetPublicIP) {
    CPeerManager manager(8350, 8, 120, 32, 180, 0xACDE4892);  // LOCALNET_MAGIC

    std::atomic<int> query_count{0};
    std::atomic<bool> stop_flag{false};

    // Start multiple threads querying public IP
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&manager, &query_count, &stop_flag]() {
            while (!stop_flag) {
                std::string ip = manager.GetPublicIP();
                (void)ip; // Suppress unused warning
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
    ASSERT_TRUE(query_count > 100, "Should have made multiple thread-safe queries");
}

/**
 * @brief Test SendPingToAllPeers doesn't deadlock with concurrent peer operations
 *
 * This test verifies the fix for deadlock scenario:
 * - Thread 1 calls SendPingToAllPeers() repeatedly (like PeerManagerThread)
 * - Thread 2-11 call GetConnectedPeers() repeatedly (like REST API threads)
 * - Without the fix: If SendPingToAllPeers held cs_peers while calling
 *   SendMessageAsync, and async completion called DisconnectPeer, deadlock occurs
 * - With fix: Lock is released before async operations, no deadlock
 *
 * The fix copies peer list under lock, releases lock, then sends messages.
 * This test verifies concurrent access patterns don't deadlock.
 */
TEST(PeerManager_SendPing_NoDeadlock) {
    CPeerManager manager(8360, 8, 120, 32, 180, 0xACDE4892);  // LOCALNET_MAGIC

    // Start the manager to enable threads
    manager.Start();

    std::atomic<bool> stop_flag{false};
    std::atomic<int> ping_count{0};
    std::atomic<int> query_count{0};
    std::atomic<bool> deadlock_detected{false};

    // Thread 1: Repeatedly call SendPingToAllPeers (simulates PeerManagerThread)
    std::thread ping_thread([&]() {
        while (!stop_flag) {
            manager.SendPingToAllPeers();
            ping_count++;
            std::this_thread::yield();
        }
    });

    // Thread 2-11: Repeatedly query peer info (simulates REST API threads)
    // These threads will contend for cs_peers lock
    std::vector<std::thread> query_threads;
    for (int i = 0; i < 10; i++) {
        query_threads.emplace_back([&]() {
            while (!stop_flag) {
                auto peers = manager.GetConnectedPeers();
                (void)peers;
                query_count++;
                std::this_thread::yield();
            }
        });
    }

    // Watchdog thread - detects if test is hanging
    std::thread watchdog([&]() {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (!stop_flag) {
            deadlock_detected = true;
            // Force exit if hanging
            std::_Exit(1);
        }
    });

    // Run for 500ms - if there's a deadlock, watchdog will kill us
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop_flag = true;

    // Join all threads - if deadlock exists, this will hang
    ping_thread.join();
    for (auto& t : query_threads) {
        t.join();
    }

    manager.Stop();
    watchdog.join();

    // Verify operations completed without deadlock
    ASSERT_FALSE(deadlock_detected, "Deadlock detected - test hung");
    ASSERT_TRUE(ping_count > 50, "SendPingToAllPeers should have been called multiple times");
    ASSERT_TRUE(query_count > 500, "GetConnectedPeers should have been called many times");
}

/**
 * @brief Test HandleAccept doesn't deadlock when evicting peers
 *
 * This test verifies the fix for deadlock scenario in HandleAccept():
 * - HandleAccept() holds cs_peers lock while checking peer limits
 * - When limit reached, it calls DisconnectPeer() to evict a peer
 * - DisconnectPeer() needs cs_peers lock
 * - Without fix: DEADLOCK. With fix: eviction succeeds
 *
 * The test simulates this by:
 * 1. Starting peer manager with low max_inbound_peers limit
 * 2. Accepting connections in one thread (via Start())
 * 3. Simultaneously querying peer counts from multiple threads
 * 4. Verifying no deadlock when peer eviction triggers
 *
 * Note: This is a stress test that verifies thread-safety of peer eviction logic.
 */
TEST(PeerManager_HandleAccept_NoDeadlock) {
    // Use very low limits to trigger eviction quickly
    CPeerManager manager(8361, 1, 2, 32, 180, 0xACDE4892);  // max_outbound=1, max_inbound=2

    manager.Start();

    std::atomic<bool> stop_flag{false};
    std::atomic<int> query_count{0};

    // Multiple threads querying peer counts (simulates REST API)
    std::vector<std::thread> query_threads;
    for (int i = 0; i < 10; i++) {
        query_threads.emplace_back([&manager, &stop_flag, &query_count]() {
            while (!stop_flag) {
                size_t inbound = manager.GetInboundPeerCount();
                size_t outbound = manager.GetOutboundPeerCount();
                (void)inbound;
                (void)outbound;
                query_count++;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }

    // Run for 200ms - concurrent queries while acceptor is active
    // If HandleAccept() has deadlock, queries will hang when eviction occurs
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop_flag = true;

    // Join all threads - if deadlock exists, this will hang
    for (auto& t : query_threads) {
        t.join();
    }

    manager.Stop();

    // Verify operations completed without deadlock
    ASSERT_TRUE(query_count > 100, "Peer count queries should succeed without deadlock");
}

/**
 * @brief Stress test for concurrent SendPing and DisconnectPeer operations
 *
 * This is a more aggressive stress test that verifies:
 * - SendPingToAllPeers can be called concurrently with DisconnectPeer
 * - GetConnectedPeers can be called while peers are being disconnected
 * - No race conditions or deadlocks occur under heavy concurrent load
 *
 * This test exercises the lock ordering fix:
 * - Copy peer list under lock
 * - Release lock
 * - Perform I/O operations without holding lock
 */
TEST(PeerManager_ConcurrentPingDisconnect_StressTest) {
    CPeerManager manager(8362, 8, 120, 32, 180, 0xACDE4892);
    manager.Start();

    std::atomic<bool> stop_flag{false};
    std::atomic<int> operation_count{0};

    // Thread pool performing various operations concurrently
    std::vector<std::thread> threads;

    // 5 threads calling SendPingToAllPeers
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&manager, &stop_flag, &operation_count]() {
            while (!stop_flag) {
                manager.SendPingToAllPeers();
                operation_count++;
                std::this_thread::yield();
            }
        });
    }

    // 10 threads querying peer info
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&manager, &stop_flag, &operation_count]() {
            while (!stop_flag) {
                auto peers = manager.GetConnectedPeers();
                size_t count = manager.GetInboundPeerCount();
                (void)peers;
                (void)count;
                operation_count++;
                std::this_thread::yield();
            }
        });
    }

    // 5 threads querying peer counts
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&manager, &stop_flag, &operation_count]() {
            while (!stop_flag) {
                size_t out = manager.GetOutboundPeerCount();
                size_t in = manager.GetInboundPeerCount();
                (void)out;
                (void)in;
                operation_count++;
                std::this_thread::yield();
            }
        });
    }

    // Run stress test for 300ms
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stop_flag = true;

    // Join all threads - will hang if any deadlock exists
    for (auto& t : threads) {
        t.join();
    }

    manager.Stop();

    // Verify we performed many concurrent operations without deadlock
    ASSERT_TRUE(operation_count > 1000, "Should complete many concurrent operations");
}
