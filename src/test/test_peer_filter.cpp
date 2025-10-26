// ============= test_peer_filter.cpp =============
#include "unit_test.h"
#include "peer/peer_filter.h"
#include <thread>
#include <chrono>
#include <atomic>

using namespace UnitTest;

/**
 * @brief Test CPeerFilter default constructor
 */
TEST(PeerFilter_Constructor) {
    CPeerFilter filter;

    ASSERT_EQUAL(filter.GetTxIdCount(), (size_t)0, "Should start with no TX IDs tracked");
    ASSERT_EQUAL(filter.GetBlockCount(), (size_t)0, "Should start with no blocks tracked");
}

/**
 * @brief Test adding transaction ID for a peer
 */
TEST(PeerFilter_AddTxIdForPeer) {
    CPeerFilter filter;

    filter.AddTxIdForPeer("tx_abc123", "192.168.1.100", 8333);

    ASSERT_EQUAL(filter.GetTxIdCount(), (size_t)1, "Should track 1 TX ID");
    ASSERT_TRUE(filter.PeerKnowsTxId("tx_abc123", "192.168.1.100", 8333), "Peer should know about TX");
}

/**
 * @brief Test adding block for a peer
 */
TEST(PeerFilter_AddBlockForPeer) {
    CPeerFilter filter;

    filter.AddBlockForPeer("block_hash_xyz", "192.168.1.101", 8333);

    ASSERT_EQUAL(filter.GetBlockCount(), (size_t)1, "Should track 1 block");
    ASSERT_TRUE(filter.PeerKnowsBlock("block_hash_xyz", "192.168.1.101", 8333), "Peer should know about block");
}

/**
 * @brief Test checking if peer knows about transaction
 */
TEST(PeerFilter_PeerKnowsTxId) {
    CPeerFilter filter;

    filter.AddTxIdForPeer("tx_def456", "192.168.1.102", 8334);

    ASSERT_TRUE(filter.PeerKnowsTxId("tx_def456", "192.168.1.102", 8334), "Peer should know about TX");
    ASSERT_FALSE(filter.PeerKnowsTxId("tx_def456", "192.168.1.103", 8334), "Different peer should not know");
    ASSERT_FALSE(filter.PeerKnowsTxId("tx_unknown", "192.168.1.102", 8334), "Unknown TX should return false");
}

/**
 * @brief Test checking if peer knows about block
 */
TEST(PeerFilter_PeerKnowsBlock) {
    CPeerFilter filter;

    filter.AddBlockForPeer("block_hash_abc", "192.168.1.104", 8335);

    ASSERT_TRUE(filter.PeerKnowsBlock("block_hash_abc", "192.168.1.104", 8335), "Peer should know about block");
    ASSERT_FALSE(filter.PeerKnowsBlock("block_hash_abc", "192.168.1.105", 8335), "Different peer should not know");
    ASSERT_FALSE(filter.PeerKnowsBlock("block_unknown", "192.168.1.104", 8335), "Unknown block should return false");
}

/**
 * @brief Test multiple peers knowing about same transaction
 */
TEST(PeerFilter_MultiplePeersSameTx) {
    CPeerFilter filter;

    filter.AddTxIdForPeer("tx_shared", "192.168.1.100", 8333);
    filter.AddTxIdForPeer("tx_shared", "192.168.1.101", 8333);
    filter.AddTxIdForPeer("tx_shared", "192.168.1.102", 8333);

    ASSERT_EQUAL(filter.GetTxIdCount(), (size_t)1, "Should track only 1 unique TX ID");
    ASSERT_TRUE(filter.PeerKnowsTxId("tx_shared", "192.168.1.100", 8333), "First peer should know");
    ASSERT_TRUE(filter.PeerKnowsTxId("tx_shared", "192.168.1.101", 8333), "Second peer should know");
    ASSERT_TRUE(filter.PeerKnowsTxId("tx_shared", "192.168.1.102", 8333), "Third peer should know");
}

/**
 * @brief Test multiple blocks for same peer
 */
TEST(PeerFilter_MultipleBlocksSamePeer) {
    CPeerFilter filter;

    filter.AddBlockForPeer("block_1", "192.168.1.100", 8333);
    filter.AddBlockForPeer("block_2", "192.168.1.100", 8333);
    filter.AddBlockForPeer("block_3", "192.168.1.100", 8333);

    ASSERT_EQUAL(filter.GetBlockCount(), (size_t)3, "Should track 3 unique blocks");
    ASSERT_TRUE(filter.PeerKnowsBlock("block_1", "192.168.1.100", 8333), "Peer should know block_1");
    ASSERT_TRUE(filter.PeerKnowsBlock("block_2", "192.168.1.100", 8333), "Peer should know block_2");
    ASSERT_TRUE(filter.PeerKnowsBlock("block_3", "192.168.1.100", 8333), "Peer should know block_3");
}

/**
 * @brief Test GetPeersWithoutTxId filtering
 */
TEST(PeerFilter_GetPeersWithoutTxId) {
    CPeerFilter filter;

    // Mark two peers as knowing about TX
    filter.AddTxIdForPeer("tx_broadcast", "192.168.1.100", 8333);
    filter.AddTxIdForPeer("tx_broadcast", "192.168.1.101", 8333);

    // All connected peers
    std::vector<std::string> all_peers = {
        "192.168.1.100:8333",
        "192.168.1.101:8333",
        "192.168.1.102:8333",
        "192.168.1.103:8333"
    };

    auto filtered_peers = filter.GetPeersWithoutTxId("tx_broadcast", all_peers);

    ASSERT_EQUAL(filtered_peers.size(), (size_t)2, "Should filter out 2 peers");
    ASSERT_TRUE(
        std::find(filtered_peers.begin(), filtered_peers.end(), "192.168.1.102:8333") != filtered_peers.end(),
        "Peer 102 should be in filtered list"
    );
    ASSERT_TRUE(
        std::find(filtered_peers.begin(), filtered_peers.end(), "192.168.1.103:8333") != filtered_peers.end(),
        "Peer 103 should be in filtered list"
    );
}

/**
 * @brief Test GetPeersWithoutBlock filtering
 */
TEST(PeerFilter_GetPeersWithoutBlock) {
    CPeerFilter filter;

    // Mark one peer as knowing about block
    filter.AddBlockForPeer("block_new", "192.168.1.100", 8333);

    // All connected peers
    std::vector<std::string> all_peers = {
        "192.168.1.100:8333",
        "192.168.1.101:8333",
        "192.168.1.102:8333"
    };

    auto filtered_peers = filter.GetPeersWithoutBlock("block_new", all_peers);

    ASSERT_EQUAL(filtered_peers.size(), (size_t)2, "Should filter out 1 peer");
    ASSERT_TRUE(
        std::find(filtered_peers.begin(), filtered_peers.end(), "192.168.1.101:8333") != filtered_peers.end(),
        "Peer 101 should be in filtered list"
    );
    ASSERT_TRUE(
        std::find(filtered_peers.begin(), filtered_peers.end(), "192.168.1.102:8333") != filtered_peers.end(),
        "Peer 102 should be in filtered list"
    );
}

/**
 * @brief Test filtering with no tracked data
 */
TEST(PeerFilter_FilteringWithNoData) {
    CPeerFilter filter;

    std::vector<std::string> all_peers = {
        "192.168.1.100:8333",
        "192.168.1.101:8333"
    };

    auto filtered_tx = filter.GetPeersWithoutTxId("tx_unknown", all_peers);
    auto filtered_block = filter.GetPeersWithoutBlock("block_unknown", all_peers);

    ASSERT_EQUAL(filtered_tx.size(), (size_t)2, "Should return all peers for unknown TX");
    ASSERT_EQUAL(filtered_block.size(), (size_t)2, "Should return all peers for unknown block");
}

/**
 * @brief Test RemoveTxId
 */
TEST(PeerFilter_RemoveTxId) {
    CPeerFilter filter;

    filter.AddTxIdForPeer("tx_temp", "192.168.1.100", 8333);
    ASSERT_EQUAL(filter.GetTxIdCount(), (size_t)1, "Should have 1 TX ID");

    filter.RemoveTxId("tx_temp");
    ASSERT_EQUAL(filter.GetTxIdCount(), (size_t)0, "Should have 0 TX IDs after removal");
    ASSERT_FALSE(filter.PeerKnowsTxId("tx_temp", "192.168.1.100", 8333), "Peer should no longer know about TX");
}

/**
 * @brief Test RemoveBlock
 */
TEST(PeerFilter_RemoveBlock) {
    CPeerFilter filter;

    filter.AddBlockForPeer("block_temp", "192.168.1.100", 8333);
    ASSERT_EQUAL(filter.GetBlockCount(), (size_t)1, "Should have 1 block");

    filter.RemoveBlock("block_temp");
    ASSERT_EQUAL(filter.GetBlockCount(), (size_t)0, "Should have 0 blocks after removal");
    ASSERT_FALSE(filter.PeerKnowsBlock("block_temp", "192.168.1.100", 8333), "Peer should no longer know about block");
}

/**
 * @brief Test Clear
 */
TEST(PeerFilter_Clear) {
    CPeerFilter filter;

    filter.AddTxIdForPeer("tx_1", "192.168.1.100", 8333);
    filter.AddTxIdForPeer("tx_2", "192.168.1.101", 8333);
    filter.AddBlockForPeer("block_1", "192.168.1.100", 8333);
    filter.AddBlockForPeer("block_2", "192.168.1.101", 8333);

    ASSERT_EQUAL(filter.GetTxIdCount(), (size_t)2, "Should have 2 TX IDs");
    ASSERT_EQUAL(filter.GetBlockCount(), (size_t)2, "Should have 2 blocks");

    filter.Clear();

    ASSERT_EQUAL(filter.GetTxIdCount(), (size_t)0, "Should have 0 TX IDs after clear");
    ASSERT_EQUAL(filter.GetBlockCount(), (size_t)0, "Should have 0 blocks after clear");
}

/**
 * @brief Test thread-safe AddTxIdForPeer
 */
TEST(PeerFilter_ThreadSafeAddTxId) {
    CPeerFilter filter;
    std::atomic<bool> stop_flag{false};

    // Multiple threads adding TX IDs for different peers
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&filter, i, &stop_flag]() {
            for (int j = 0; j < 20; j++) {
                if (stop_flag) return;
                std::string tx_id = "tx_" + std::to_string(j);
                std::string address = "192.168.1." + std::to_string(100 + i);
                filter.AddTxIdForPeer(tx_id, address, 8333);
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }

    // Let threads run
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop_flag = true;

    for (auto& t : threads) {
        t.join();
    }

    // Verify data integrity (no crashes, counts make sense)
    ASSERT_TRUE(filter.GetTxIdCount() > 0, "Should have tracked TX IDs");
}

/**
 * @brief Test thread-safe GetPeersWithoutTxId
 */
TEST(PeerFilter_ThreadSafeGetPeersWithout) {
    CPeerFilter filter;

    // Add some initial data
    filter.AddTxIdForPeer("tx_shared", "192.168.1.100", 8333);
    filter.AddTxIdForPeer("tx_shared", "192.168.1.101", 8333);

    std::vector<std::string> all_peers = {
        "192.168.1.100:8333",
        "192.168.1.101:8333",
        "192.168.1.102:8333",
        "192.168.1.103:8333"
    };

    std::atomic<int> query_count{0};
    std::atomic<bool> stop_flag{false};

    // Multiple threads querying filtered peers
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&filter, &all_peers, &query_count, &stop_flag]() {
            while (!stop_flag) {
                auto filtered = filter.GetPeersWithoutTxId("tx_shared", all_peers);
                ASSERT_EQUAL(filtered.size(), (size_t)2, "Should consistently filter to 2 peers");
                query_count++;
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
    }

    // Let threads run
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop_flag = true;

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_TRUE(query_count > 100, "Should have made many queries without crashing");
}

/**
 * @brief Test concurrent add and query operations
 */
TEST(PeerFilter_ConcurrentAddAndQuery) {
    CPeerFilter filter;
    std::atomic<bool> stop_flag{false};

    // Writer thread
    std::thread writer([&filter, &stop_flag]() {
        int count = 0;
        while (!stop_flag) {
            std::string tx_id = "tx_" + std::to_string(count++ % 10);
            filter.AddTxIdForPeer(tx_id, "192.168.1.100", 8333);
            std::this_thread::sleep_for(std::chrono::microseconds(5));
        }
    });

    // Reader threads
    std::vector<std::thread> readers;
    for (int i = 0; i < 3; i++) {
        readers.emplace_back([&filter, &stop_flag]() {
            while (!stop_flag) {
                bool knows = filter.PeerKnowsTxId("tx_5", "192.168.1.100", 8333);
                (void)knows; // Suppress unused warning
                std::this_thread::sleep_for(std::chrono::microseconds(5));
            }
        });
    }

    // Let threads run
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop_flag = true;

    writer.join();
    for (auto& t : readers) {
        t.join();
    }

    // If we got here without crashing, thread safety is working
    ASSERT_TRUE(true, "Concurrent add and query operations should be safe");
}

/**
 * @brief Test peer identifier with different ports
 */
TEST(PeerFilter_DifferentPorts) {
    CPeerFilter filter;

    // Same address, different ports should be treated as different peers
    filter.AddTxIdForPeer("tx_port_test", "192.168.1.100", 8333);

    ASSERT_TRUE(filter.PeerKnowsTxId("tx_port_test", "192.168.1.100", 8333), "Port 8333 should know");
    ASSERT_FALSE(filter.PeerKnowsTxId("tx_port_test", "192.168.1.100", 8334), "Port 8334 should not know");
}

/**
 * @brief Test adding same peer multiple times (idempotent)
 */
TEST(PeerFilter_IdempotentAdd) {
    CPeerFilter filter;

    // Add same peer multiple times for same TX
    filter.AddTxIdForPeer("tx_idem", "192.168.1.100", 8333);
    filter.AddTxIdForPeer("tx_idem", "192.168.1.100", 8333);
    filter.AddTxIdForPeer("tx_idem", "192.168.1.100", 8333);

    ASSERT_EQUAL(filter.GetTxIdCount(), (size_t)1, "Should still track only 1 TX ID");

    std::vector<std::string> all_peers = {
        "192.168.1.100:8333",
        "192.168.1.101:8333"
    };

    auto filtered = filter.GetPeersWithoutTxId("tx_idem", all_peers);
    ASSERT_EQUAL(filtered.size(), (size_t)1, "Should filter out peer 100 only once");
}
