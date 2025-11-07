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

/**
 * @brief Test CTransaction serialization
 */
TEST(Transaction_Serialize) {
    std::vector<uint8_t> data = {'h', 'e', 'l', 'l', 'o'};
    CTransaction tx("alice", "bob", data, 100, TransactionType::TRANSFER, "{\"test\":\"data\"}");

    std::string serialized = tx.Serialize();

    // Check that serialized data is not empty
    ASSERT_TRUE(serialized.length() > 0, "Serialized data should not be empty");

    // Minimum size check: 4 + owner + 4 + target + 4 + data + 8 + 8 + 1 + 4 + metadata
    size_t expected_min_size = 4 + 5 + 4 + 3 + 4 + 5 + 8 + 8 + 1 + 4 + 15;
    ASSERT_TRUE(serialized.length() >= expected_min_size, "Serialized size should be at least minimum");
}

/**
 * @brief Test CTransaction deserialization
 */
TEST(Transaction_Deserialize) {
    // Create original transaction
    std::vector<uint8_t> data = {'w', 'o', 'r', 'l', 'd'};
    CTransaction tx_orig("sender", "receiver", data, 250, TransactionType::STORAGE, "{\"file\":\"test.txt\"}");

    // Serialize
    std::string serialized = tx_orig.Serialize();

    // Deserialize
    std::shared_ptr<CTransaction> p_tx = CTransaction::Deserialize(serialized);

    // Verify deserialization succeeded
    ASSERT_TRUE(p_tx != nullptr, "Deserialization should succeed");

    // Verify all fields match
    ASSERT_EQUAL(p_tx->m_str_owner, tx_orig.m_str_owner, "Owner should match");
    ASSERT_EQUAL(p_tx->m_str_target, tx_orig.m_str_target, "Target should match");
    ASSERT_EQUAL(p_tx->m_data.size(), tx_orig.m_data.size(), "Data size should match");
    ASSERT_EQUAL(p_tx->m_n_reward, tx_orig.m_n_reward, "Reward should match");
    ASSERT_EQUAL(p_tx->m_n_timestamp, tx_orig.m_n_timestamp, "Timestamp should match");
    ASSERT_EQUAL(static_cast<int>(p_tx->m_type), static_cast<int>(tx_orig.m_type), "Type should match");
    ASSERT_EQUAL(p_tx->m_str_metadata, tx_orig.m_str_metadata, "Metadata should match");
    ASSERT_EQUAL(p_tx->m_id.GetData(), tx_orig.m_id.GetData(), "Transaction ID should match");
}

/**
 * @brief Test CTransaction deserialization with invalid data
 */
TEST(Transaction_Deserialize_Invalid) {
    // Test with empty data
    std::shared_ptr<CTransaction> p_tx1 = CTransaction::Deserialize("");
    ASSERT_TRUE(p_tx1 == nullptr, "Empty data should fail deserialization");

    // Test with truncated data
    std::string short_data = "abc";
    std::shared_ptr<CTransaction> p_tx2 = CTransaction::Deserialize(short_data);
    ASSERT_TRUE(p_tx2 == nullptr, "Truncated data should fail deserialization");

    // Test with partial header
    std::string partial_data(20, 'x');
    std::shared_ptr<CTransaction> p_tx3 = CTransaction::Deserialize(partial_data);
    ASSERT_TRUE(p_tx3 == nullptr, "Partial header should fail deserialization");
}

/**
 * @brief Test CTransaction serialization roundtrip with large data
 */
TEST(Transaction_Serialize_Roundtrip_LargeData) {
    // Create transaction with large data
    std::vector<uint8_t> large_data(5000, 'Z');
    CTransaction tx_orig("from_address", "to_address", large_data, 1000, TransactionType::COMPUTE, "{\"cpu\":4}");

    // Serialize and deserialize
    std::string serialized = tx_orig.Serialize();
    std::shared_ptr<CTransaction> p_tx = CTransaction::Deserialize(serialized);

    // Verify
    ASSERT_TRUE(p_tx != nullptr, "Large data deserialization should succeed");
    ASSERT_EQUAL(p_tx->m_data.size(), (size_t)5000, "Large data size should be preserved");
    ASSERT_EQUAL(p_tx->m_id.GetData(), tx_orig.m_id.GetData(), "Large data transaction ID should match");
}
