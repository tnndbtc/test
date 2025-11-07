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
    ASSERT_TRUE(block.GetNonce() > 0, "Mined block should have nonce");
}

/**
 * @brief Test CBlock serialization
 */
TEST(Block_Serialize) {
    CHash previous_hash("previous_block");
    CBlock block(previous_hash, 5, "test_miner");

    // Add transactions
    std::vector<uint8_t> data = {'t', 'e', 's', 't'};
    auto tx = std::make_shared<CTransaction>("alice", "bob", data, 100);
    block.AddTransaction(tx);

    // Mine to set hash
    block.Mine();

    // Serialize
    std::string serialized = block.Serialize();

    // Check that serialized data is not empty
    ASSERT_TRUE(serialized.length() > 0, "Serialized block should not be empty");

    // Minimum size: 32 (hash) + 32 (prev) + 8 (height) + 8 (timestamp) + 4 (miner_len) + miner + ...
    ASSERT_TRUE(serialized.length() > 100, "Serialized block should have reasonable size");
}

/**
 * @brief Test CBlock deserialization
 */
TEST(Block_Deserialize) {
    // Create and mine original block
    CHash previous_hash("prev_block");
    CBlock block_orig(previous_hash, 10, "miner_address");

    std::vector<uint8_t> data1 = {'d', 'a', 't', 'a', '1'};
    std::vector<uint8_t> data2 = {'d', 'a', 't', 'a', '2'};
    auto tx1 = std::make_shared<CTransaction>("from1", "to1", data1, 50);
    auto tx2 = std::make_shared<CTransaction>("from2", "to2", data2, 75);
    block_orig.AddTransaction(tx1);
    block_orig.AddTransaction(tx2);

    block_orig.Mine();

    // Serialize
    std::string serialized = block_orig.Serialize();

    // Deserialize
    std::shared_ptr<CBlock> p_block = CBlock::Deserialize(serialized);

    // Verify deserialization succeeded
    ASSERT_TRUE(p_block != nullptr, "Deserialization should succeed");

    // Verify block fields match
    ASSERT_EQUAL(p_block->GetHeight(), block_orig.GetHeight(), "Height should match");
    ASSERT_EQUAL(p_block->GetMiner(), block_orig.GetMiner(), "Miner should match");
    ASSERT_EQUAL(p_block->GetHash().GetData(), block_orig.GetHash().GetData(), "Hash should match");
    ASSERT_EQUAL(p_block->GetPreviousBlock().GetData(), block_orig.GetPreviousBlock().GetData(), "Previous hash should match");
    ASSERT_EQUAL(p_block->GetTransactions().size(), (size_t)2, "Should have 2 transactions");
    ASSERT_EQUAL(p_block->GetDifficulty(), block_orig.GetDifficulty(), "Difficulty should match");
    ASSERT_EQUAL(p_block->GetNonce(), block_orig.GetNonce(), "Nonce should match");
}

/**
 * @brief Test CBlock deserialization with invalid data
 */
TEST(Block_Deserialize_Invalid) {
    // Test with empty data
    std::shared_ptr<CBlock> p_block1 = CBlock::Deserialize("");
    ASSERT_TRUE(p_block1 == nullptr, "Empty data should fail deserialization");

    // Test with truncated data
    std::string short_data = "short";
    std::shared_ptr<CBlock> p_block2 = CBlock::Deserialize(short_data);
    ASSERT_TRUE(p_block2 == nullptr, "Truncated data should fail deserialization");

    // Test with partial header (less than minimum size)
    std::string partial_data(50, 'x');
    std::shared_ptr<CBlock> p_block3 = CBlock::Deserialize(partial_data);
    ASSERT_TRUE(p_block3 == nullptr, "Partial header should fail deserialization");
}

/**
 * @brief Test CBlock serialization roundtrip with multiple transactions
 */
TEST(Block_Serialize_Roundtrip_MultipleTransactions) {
    CHash previous_hash("test_prev");
    CBlock block_orig(previous_hash, 42, "test_miner_addr");

    // Add multiple transactions
    for (int i = 0; i < 5; ++i) {
        std::vector<uint8_t> data = {'t', 'x', static_cast<uint8_t>('0' + i)};
        auto tx = std::make_shared<CTransaction>("sender" + std::to_string(i),
                                                  "receiver" + std::to_string(i),
                                                  data, 100 + i);
        block_orig.AddTransaction(tx);
    }

    block_orig.Mine();

    // Serialize and deserialize
    std::string serialized = block_orig.Serialize();
    std::shared_ptr<CBlock> p_block = CBlock::Deserialize(serialized);

    // Verify
    ASSERT_TRUE(p_block != nullptr, "Multiple transactions deserialization should succeed");
    ASSERT_EQUAL(p_block->GetTransactions().size(), (size_t)5, "Should preserve all 5 transactions");
    ASSERT_EQUAL(p_block->GetHash().GetData(), block_orig.GetHash().GetData(), "Hash should match after roundtrip");

    // Verify each transaction
    for (size_t i = 0; i < 5; ++i) {
        ASSERT_EQUAL(p_block->GetTransactions()[i]->m_id.GetData(),
                     block_orig.GetTransactions()[i]->m_id.GetData(),
                     "Transaction ID should match");
    }
}

/**
 * @brief Test CBlock::CreateGenesisBlock()
 */
TEST(Block_CreateGenesisBlock) {
    // Create genesis block
    std::shared_ptr<CBlock> p_genesis = CBlock::CreateGenesisBlock();

    // Verify genesis block was created
    ASSERT_TRUE(p_genesis != nullptr, "Genesis block should be created");

    // Verify height is 0
    ASSERT_EQUAL(p_genesis->GetHeight(), (int64_t)0, "Genesis block height should be 0");

    // Verify miner is "genesis"
    ASSERT_EQUAL(p_genesis->GetMiner(), std::string("genesis"), "Genesis block miner should be 'genesis'");

    // Verify previous block hash is all zeros (64 hex characters)
    std::string expected_zero_hash(64, '0');
    ASSERT_EQUAL(p_genesis->GetPreviousBlock().GetData(), expected_zero_hash, "Genesis block previous hash should be all zeros");

    // Verify timestamp is set
    ASSERT_TRUE(p_genesis->GetTimestamp() > 0, "Genesis block timestamp should be set");

    // Verify difficulty is set
    ASSERT_EQUAL(p_genesis->GetDifficulty(), (uint64_t)1000, "Genesis block difficulty should be 1000");

    // Verify no transactions initially
    ASSERT_EQUAL(p_genesis->GetTransactions().size(), (size_t)0, "Genesis block should have no transactions initially");
}
