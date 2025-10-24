// ============= test_blockweave.cpp =============
#include "unit_test.h"
#include "blockcore/blockweave.h"
#include "blockcore/transaction.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>

using namespace UnitTest;

/**
 * @brief Test CBlockweave default constructor
 */
TEST(Blockweave_Constructor) {
    CBlockweave blockweave;

    ASSERT_EQUAL(blockweave.GetMempoolSize(), (size_t)0, "Mempool should be empty initially");
    ASSERT_FALSE(blockweave.IsMiningEnabled(), "Mining should be disabled initially");
}

/**
 * @brief Test CBlockweave transaction management
 */
TEST(Blockweave_AddTransaction) {
    CBlockweave blockweave;

    std::vector<uint8_t> data = {'t', 'e', 's', 't'};
    auto tx = std::make_shared<CTransaction>("sender", "receiver", data, 50);

    blockweave.AddTransaction(tx);

    ASSERT_EQUAL(blockweave.GetMempoolSize(), (size_t)1, "Mempool should have 1 transaction");
}

/**
 * @brief Test CBlockweave multiple transactions
 */
TEST(Blockweave_MultipleTransactions) {
    CBlockweave blockweave;

    // Add multiple transactions
    for (int i = 0; i < 10; i++) {
        std::vector<uint8_t> data = {'t', 'x', (uint8_t)('0' + i)};
        auto tx = std::make_shared<CTransaction>("from" + std::to_string(i),
                                                  "to" + std::to_string(i),
                                                  data, i * 10);
        blockweave.AddTransaction(tx);
    }

    ASSERT_EQUAL(blockweave.GetMempoolSize(), (size_t)10, "Mempool should have 10 transactions");
}

/**
 * @brief Test CBlockweave mining control
 */
TEST(Blockweave_MiningControl) {
    CBlockweave blockweave;

    ASSERT_FALSE(blockweave.IsMiningEnabled(), "Mining should be disabled initially");

    blockweave.StartMining();
    ASSERT_TRUE(blockweave.IsMiningEnabled(), "Mining should be enabled after StartMining");

    blockweave.StopMining();
    ASSERT_FALSE(blockweave.IsMiningEnabled(), "Mining should be disabled after StopMining");
}

/**
 * @brief Test CBlockweave mining lifecycle
 */
TEST(Blockweave_MiningLifecycle) {
    CBlockweave blockweave;

    // Start/stop multiple times
    for (int i = 0; i < 3; i++) {
        blockweave.StartMining();
        ASSERT_TRUE(blockweave.IsMiningEnabled(), "Mining should be enabled");

        blockweave.StopMining();
        ASSERT_FALSE(blockweave.IsMiningEnabled(), "Mining should be disabled");
    }
}

/**
 * @brief Test CBlockweave thread-safe transaction addition
 */
TEST(Blockweave_ThreadSafeAddTransaction) {
    CBlockweave blockweave;
    std::atomic<int> tx_count{0};
    const int n_threads = 5;
    const int n_tx_per_thread = 20;

    std::vector<std::thread> threads;
    for (int i = 0; i < n_threads; i++) {
        threads.emplace_back([&blockweave, &tx_count, i]() {
            for (int j = 0; j < n_tx_per_thread; j++) {
                std::vector<uint8_t> data = {'t', 'x', (uint8_t)i, (uint8_t)j};
                auto tx = std::make_shared<CTransaction>(
                    "from" + std::to_string(i),
                    "to" + std::to_string(j),
                    data,
                    i * 10 + j
                );
                blockweave.AddTransaction(tx);
                tx_count++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQUAL(blockweave.GetMempoolSize(), (size_t)(n_threads * n_tx_per_thread),
                 "All transactions should be added");
    ASSERT_EQUAL(tx_count.load(), n_threads * n_tx_per_thread,
                 "Transaction count should match");
}

/**
 * @brief Test CBlockweave thread-safe mining control
 */
TEST(Blockweave_ThreadSafeMiningControl) {
    CBlockweave blockweave;
    std::atomic<bool> stop_flag{false};
    std::atomic<int> start_count{0};
    std::atomic<int> stop_count{0};

    // Thread 1: Repeatedly start mining
    std::thread starter([&blockweave, &stop_flag, &start_count]() {
        while (!stop_flag) {
            blockweave.StartMining();
            start_count++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // Thread 2: Repeatedly stop mining
    std::thread stopper([&blockweave, &stop_flag, &stop_count]() {
        while (!stop_flag) {
            blockweave.StopMining();
            stop_count++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // Thread 3: Check mining status
    std::thread checker([&blockweave, &stop_flag]() {
        while (!stop_flag) {
            bool status = blockweave.IsMiningEnabled();
            (void)status; // Suppress unused warning
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    // Let threads run
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop_flag = true;

    starter.join();
    stopper.join();
    checker.join();

    ASSERT_TRUE(start_count > 0, "Should have started mining multiple times");
    ASSERT_TRUE(stop_count > 0, "Should have stopped mining multiple times");
}
