// ============= test_block.cpp =============
#include "unit_test.h"
#include "blockcore/block.h"
#include "blockcore/transaction.h"
#include <memory>

using namespace UnitTest;

/**
 * @brief Test CBlock constructor
 */
TEST(Block_Constructor) {
    CHash previous_hash("previous_block_hash");
    CBlock block(previous_hash, 1, "miner_address");

    ASSERT_EQUAL(block.GetHeight(), (int64_t)1, "Height should be 1");
    ASSERT_TRUE(block.GetTimestamp() > 0, "Timestamp should be set");
    ASSERT_EQUAL(block.GetMiner(), std::string("miner_address"), "Miner address should match");
    ASSERT_EQUAL(block.GetPreviousBlock().GetData(), previous_hash.GetData(), "Previous block hash should match");
}

/**
 * @brief Test CBlock hash computation after mining
 */
TEST(Block_HashComputation) {
    CHash previous_hash("previous");
    CBlock block1(previous_hash, 1, "miner1");
    CBlock block2(previous_hash, 2, "miner1");

    // Mine both blocks to compute their hashes
    block1.Mine();
    block2.Mine();

    // Different heights should produce different hashes after mining
    ASSERT_TRUE(block1.GetHash().GetData().length() > 0, "Block hash should not be empty after mining");
    ASSERT_TRUE(block2.GetHash().GetData().length() > 0, "Block hash should not be empty after mining");
    ASSERT_NOT_EQUAL(block1.GetHash().GetData(), block2.GetHash().GetData(), "Different blocks should have different hashes");
}

/**
 * @brief Test CBlock with transactions
 */
TEST(Block_WithTransactions) {
    CHash previous_hash("previous");
    CBlock block(previous_hash, 1, "miner");

    // Create some test transactions
    std::vector<uint8_t> data1 = {'t', 'e', 's', 't', '1'};
    std::vector<uint8_t> data2 = {'t', 'e', 's', 't', '2'};

    auto tx1 = std::make_shared<CTransaction>("from1", "to1", data1, 10);
    auto tx2 = std::make_shared<CTransaction>("from2", "to2", data2, 20);

    block.AddTransaction(tx1);
    block.AddTransaction(tx2);

    ASSERT_EQUAL(block.GetTransactions().size(), (size_t)2, "Block should have 2 transactions");
}

/**
 * @brief Test CBlock mining
 */
TEST(Block_Mining) {
    CHash previous_hash("previous");
    CBlock block(previous_hash, 1, "miner");

    // Add a transaction
    std::vector<uint8_t> data = {'d', 'a', 't', 'a'};
    auto tx = std::make_shared<CTransaction>("from", "to", data, 50);
    block.AddTransaction(tx);

    // Mine the block
    block.Mine();

    ASSERT_TRUE(block.GetHash().GetData().length() > 0, "Mined block should have hash");
    ASSERT_TRUE(block.GetNonce().length() > 0, "Mined block should have nonce");
}
