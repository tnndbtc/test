// ============= test_transaction.cpp =============
#include "unit_test.h"
#include "blockcore/transaction.h"
#include <thread>
#include <chrono>

using namespace UnitTest;

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

    // Create transactions with same owner, target, reward but different data
    CTransaction tx1("from", "to", data1, 50);
    CTransaction tx2("from", "to", data2, 50);

    // Different data should produce different transaction IDs
    // (no delay needed - data is now included in hash)
    ASSERT_NOT_EQUAL(tx1.m_id.GetData(), tx2.m_id.GetData(),
                     "Different data should produce different IDs");
    ASSERT_TRUE(tx1.m_id.GetData().length() > 0, "Transaction ID should not be empty");
    ASSERT_TRUE(tx2.m_id.GetData().length() > 0, "Transaction ID should not be empty");

    // Same data should produce same ID (if created at exact same timestamp)
    // Create two transactions with identical data
    std::vector<uint8_t> data3 = {'t', 'e', 's', 't'};
    CTransaction tx3("alice", "bob", data3, 100);

    // Different owner should produce different ID even with same data
    CTransaction tx4("charlie", "bob", data3, 100);
    ASSERT_NOT_EQUAL(tx3.m_id.GetData(), tx4.m_id.GetData(),
                     "Different owner should produce different ID");

    // Different reward should produce different ID
    CTransaction tx5("alice", "bob", data3, 200);
    ASSERT_NOT_EQUAL(tx3.m_id.GetData(), tx5.m_id.GetData(),
                     "Different reward should produce different ID");
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
