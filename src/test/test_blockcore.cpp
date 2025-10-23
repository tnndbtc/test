// ============= test_blockcore.cpp =============
#include "unit_test.h"
#include "blockcore/blockweave.h"
#include "blockcore/transaction.h"
#include "wallet/wallet.h"
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

/**
 * @brief Test CTransaction constructor
 */
TEST(Transaction_Constructor) {
    std::vector<uint8_t> data = {'d', 'a', 't', 'a'};
    CTransaction tx("sender_addr", "receiver_addr", data, 100);

    ASSERT_EQUAL(tx.m_str_owner, std::string("sender_addr"), "Owner address should match");
    ASSERT_EQUAL(tx.m_str_target, std::string("receiver_addr"), "Target address should match");
    ASSERT_EQUAL(tx.m_n_reward, (uint64_t)100, "Reward should be 100");
    ASSERT_EQUAL(tx.m_data.size(), (size_t)4, "Data size should be 4");
    ASSERT_EQUAL(tx.m_n_data_size, (size_t)4, "Cached data size should be 4");
}

/**
 * @brief Test CTransaction ID generation
 */
TEST(Transaction_IDGeneration) {
    std::vector<uint8_t> data1 = {'d', 'a', 't', 'a', '1'};
    std::vector<uint8_t> data2 = {'d', 'a', 't', 'a', '2'};

    CTransaction tx1("from", "to", data1, 50);
    CTransaction tx2("from", "to", data2, 50);

    // Different data should produce different transaction IDs
    ASSERT_NOT_EQUAL(tx1.m_id.GetData(), tx2.m_id.GetData(),
                     "Different transactions should have different IDs");
    ASSERT_TRUE(tx1.m_id.GetData().length() > 0, "Transaction ID should not be empty");
    ASSERT_TRUE(tx2.m_id.GetData().length() > 0, "Transaction ID should not be empty");
}

/**
 * @brief Test CTransaction with empty data
 */
TEST(Transaction_EmptyData) {
    std::vector<uint8_t> empty_data;
    CTransaction tx("from", "to", empty_data, 0);

    ASSERT_EQUAL(tx.m_data.size(), (size_t)0, "Empty data should be allowed");
    ASSERT_EQUAL(tx.m_n_data_size, (size_t)0, "Data size should be 0");
    ASSERT_TRUE(tx.m_id.GetData().length() > 0, "Transaction ID should still be generated");
}

/**
 * @brief Test CTransaction with large data
 */
TEST(Transaction_LargeData) {
    std::vector<uint8_t> large_data(10000, 'X');
    CTransaction tx("from", "to", large_data, 500);

    ASSERT_EQUAL(tx.m_data.size(), (size_t)10000, "Large data should be stored");
    ASSERT_EQUAL(tx.m_n_data_size, (size_t)10000, "Data size should be 10000");
    ASSERT_TRUE(tx.m_id.GetData().length() > 0, "Transaction ID should be generated");
}

/**
 * @brief Test CTransaction timestamp
 */
TEST(Transaction_Timestamp) {
    std::vector<uint8_t> data = {'t', 'e', 's', 't'};
    CTransaction tx("from", "to", data, 25);

    ASSERT_TRUE(tx.m_n_timestamp > 0, "Timestamp should be set automatically");
}

/**
 * @brief Test CWallet address generation
 */
TEST(Wallet_AddressGeneration) {
    CWallet wallet1;
    CWallet wallet2;

    std::string addr1 = wallet1.GetAddress();
    std::string addr2 = wallet2.GetAddress();

    ASSERT_TRUE(addr1.length() > 0, "Wallet address should not be empty");
    ASSERT_TRUE(addr2.length() > 0, "Wallet address should not be empty");
    ASSERT_NOT_EQUAL(addr1, addr2, "Different wallets should have different addresses");
}

/**
 * @brief Test CWallet transaction creation
 */
TEST(Wallet_CreateTransaction) {
    CWallet wallet;
    std::string target_address = "target_wallet_address";
    std::vector<uint8_t> data = {'p', 'a', 'y', 'm', 'e', 'n', 't'};
    uint64_t reward = 25;

    auto tx = wallet.CreateTransaction(target_address, data, reward);

    ASSERT_NOT_NULL(tx.get(), "Transaction should be created");
    ASSERT_EQUAL(tx->m_str_owner, wallet.GetAddress(), "Transaction owner address should match wallet");
    ASSERT_EQUAL(tx->m_str_target, target_address, "Transaction target address should match");
    ASSERT_EQUAL(tx->m_n_reward, reward, "Transaction reward should match");
    ASSERT_EQUAL(tx->m_data.size(), data.size(), "Transaction data size should match");
}

/**
 * @brief Test CWallet transaction creation with default reward
 */
TEST(Wallet_CreateTransactionDefaultReward) {
    CWallet wallet;
    std::string target = "recipient";
    std::vector<uint8_t> data = {'d', 'a', 't', 'a'};

    auto tx = wallet.CreateTransaction(target, data);

    ASSERT_NOT_NULL(tx.get(), "Transaction should be created");
    ASSERT_EQUAL(tx->m_n_reward, (uint64_t)0, "Default reward should be 0");
}

/**
 * @brief Test CHash constructor and data retrieval
 */
TEST(Hash_Constructor) {
    CHash hash1("test_data");
    CHash hash2("test_data");
    CHash hash3("different_data");

    ASSERT_EQUAL(hash1.GetData(), hash2.GetData(), "Same input should produce same hash");
    ASSERT_NOT_EQUAL(hash1.GetData(), hash3.GetData(), "Different input should produce different hash");
}

/**
 * @brief Test CHash empty input
 */
TEST(Hash_EmptyInput) {
    CHash hash("");

    ASSERT_TRUE(hash.GetData().length() > 0, "Hash of empty string should still produce output");
}

/**
 * @brief Test CHash uniqueness
 */
TEST(Hash_Uniqueness) {
    CHash hash1("data1");
    CHash hash2("data2");
    CHash hash3("data1");

    ASSERT_EQUAL(hash1.GetData(), hash3.GetData(), "Same input should produce same hash");
    ASSERT_NOT_EQUAL(hash1.GetData(), hash2.GetData(), "Different inputs should produce different hashes");
}
