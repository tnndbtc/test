// ============= test_blockfile.cpp =============
/**
 * @file test_blockfile.cpp
 * @brief Unit tests for CBlockFile class
 *
 * Tests block persistence, serialization, index management, and file operations.
 * Uses temporary directories for test isolation.
 */

#include "unit_test.h"
#include "blockcore/block_file.h"
#include "blockcore/block.h"
#include "blockcore/transaction.h"
#include "logger/logger.h"
#include "utils/hash.h"
#include <memory>
#include <string>
#include <vector>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <chrono>
#include <sys/stat.h>
#include <filesystem>

// ============================================================================
// Test Helper Functions
// ============================================================================

/**
 * @brief Create unique temporary directory for test isolation
 * @return Path to temporary directory
 */
std::string CreateTempDir() {
    std::string str_dir = "./test_blockfile_" + std::to_string(time(nullptr)) +
                          "_" + std::to_string(rand() % 10000);
#ifdef _WIN32
    mkdir(str_dir.c_str());
#else
    mkdir(str_dir.c_str(), 0755);
#endif
    return str_dir;
}

/**
 * @brief Recursively remove directory and all contents
 * @param str_dir Directory path to remove
 */
void CleanupDir(const std::string& str_dir) {
    // Use C++17 filesystem for cross-platform directory removal
    std::error_code ec;
    std::filesystem::remove_all(str_dir, ec);
    // Ignore errors - directory may not exist or already cleaned up
}

/**
 * @brief Check if file exists
 * @param str_path File path to check
 * @return true if file exists, false otherwise
 */
bool FileExists(const std::string& str_path) {
    std::ifstream f(str_path);
    return f.good();
}

/**
 * @brief Create test block with specified parameters
 * @param n_height Block height
 * @param str_miner Miner address
 * @param n_tx_count Number of transactions to add
 * @return Shared pointer to created block
 */
std::shared_ptr<CBlock> CreateTestBlock(int64_t n_height, const std::string& str_miner,
                                        size_t n_tx_count = 0) {
    CHash prev_hash;
    if (n_height > 0) {
        prev_hash = CHash("previous_block_hash_" + std::to_string(n_height));
    }

    auto p_block = std::make_shared<CBlock>(prev_hash, n_height, str_miner);

    // Add transactions if requested
    for (size_t i = 0; i < n_tx_count; i++) {
        std::string str_owner = "owner_" + std::to_string(i);
        std::string str_target = "target_" + std::to_string(i);
        std::vector<uint8_t> data = {
            static_cast<uint8_t>(i),
            static_cast<uint8_t>(i + 1),
            static_cast<uint8_t>(i + 2)
        };
        uint64_t n_reward = 100 + i * 10;

        auto p_tx = std::make_shared<CTransaction>(str_owner, str_target, data, n_reward);
        p_block->AddTransaction(p_tx);
    }

    // Mine the block to compute its hash
    p_block->Mine();

    // Add small delay to ensure distinct timestamps for consecutive blocks
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    return p_block;
}

// ============================================================================
// Basic Save/Load Tests
// ============================================================================

TEST(BlockFileSaveAndLoadEmptyBlock) {
    std::string str_test_dir = CreateTempDir();

    CBlockFile blockfile(str_test_dir);

    // Create empty block (no transactions)
    auto p_block = CreateTestBlock(0, "genesis_miner", 0);
    CHash hash = p_block->GetHash();

    // Save block
    bool f_saved = blockfile.SaveBlock(p_block);
    ASSERT_TRUE(f_saved, "Assertion should be true");

    // Load block
    auto p_loaded = blockfile.LoadBlock(hash);
    ASSERT_NOT_NULL(p_loaded, "Should not be null");
    ASSERT_EQUAL(p_loaded->GetHeight(), 0, "Values should match");
    ASSERT_EQUAL(p_loaded->GetMiner(), "genesis_miner", "Values should match");
    ASSERT_EQUAL(p_loaded->GetTransactions().size(), 0, "Values should match");

    // Verify hash is preserved after save/load cycle
    ASSERT_EQUAL(p_loaded->GetHash().GetData(), hash.GetData(), "Hash should be preserved");

    CleanupDir(str_test_dir);
}

TEST(BlockFileSaveAndLoadWithTransactions) {
    std::string str_test_dir = CreateTempDir();

    CBlockFile blockfile(str_test_dir);

    // Create block with 3 transactions
    auto p_block = CreateTestBlock(5, "miner_address", 3);
    CHash hash = p_block->GetHash();

    // Save block
    ASSERT_TRUE(blockfile.SaveBlock(p_block), "Assertion should be true");

    // Load block
    auto p_loaded = blockfile.LoadBlock(hash);
    ASSERT_NOT_NULL(p_loaded, "Should not be null");
    ASSERT_EQUAL(p_loaded->GetHeight(), 5, "Values should match");
    ASSERT_EQUAL(p_loaded->GetMiner(), "miner_address", "Values should match");
    ASSERT_EQUAL(p_loaded->GetTransactions().size(), 3, "Values should match");

    // Verify hash is preserved after save/load cycle
    ASSERT_EQUAL(p_loaded->GetHash().GetData(), hash.GetData(), "Hash should be preserved");

    // Verify transaction data
    const auto& transactions = p_loaded->GetTransactions();
    ASSERT_EQUAL(transactions[0]->m_str_owner, "owner_0", "Values should match");
    ASSERT_EQUAL(transactions[0]->m_str_target, "target_0", "Values should match");
    ASSERT_EQUAL(transactions[0]->m_n_reward, 100, "Values should match");

    ASSERT_EQUAL(transactions[1]->m_str_owner, "owner_1", "Values should match");
    ASSERT_EQUAL(transactions[1]->m_n_reward, 110, "Values should match");

    ASSERT_EQUAL(transactions[2]->m_str_owner, "owner_2", "Values should match");
    ASSERT_EQUAL(transactions[2]->m_n_reward, 120, "Values should match");

    CleanupDir(str_test_dir);
}

TEST(BlockFileSaveMultipleBlocks) {
    std::string str_test_dir = CreateTempDir();

    CBlockFile blockfile(str_test_dir);

    // Create and save 5 blocks
    std::vector<CHash> hashes;
    for (int i = 0; i < 5; i++) {
        auto p_block = CreateTestBlock(i, "miner_" + std::to_string(i), 2);
        hashes.push_back(p_block->GetHash());
        ASSERT_TRUE(blockfile.SaveBlock(p_block), "Assertion should be true");
    }

    // Load and verify all blocks
    for (size_t i = 0; i < hashes.size(); i++) {
        auto p_loaded = blockfile.LoadBlock(hashes[i]);
        ASSERT_NOT_NULL(p_loaded, "Should not be null");

        // Verify loaded block's hash is in the original hashes vector
        CHash loaded_hash = p_loaded->GetHash();
        bool f_found = false;
        for (const auto& hash : hashes) {
            if (loaded_hash.GetData() == hash.GetData()) {
                f_found = true;
                break;
            }
        }
        ASSERT_TRUE(f_found, "Loaded block hash should be in original hashes vector");

        ASSERT_EQUAL(p_loaded->GetHeight(), static_cast<int64_t>(i), "Values should match");
        ASSERT_EQUAL(p_loaded->GetMiner(), "miner_" + std::to_string(i), "Miner should match");
        ASSERT_EQUAL(p_loaded->GetTransactions().size(), 2, "Values should match");
    }

    CleanupDir(str_test_dir);
}

TEST(BlockFileLoadNonExistentBlock) {
    std::string str_test_dir = CreateTempDir();

    CBlockFile blockfile(str_test_dir);

    // Try to load block that doesn't exist
    CHash fake_hash("nonexistent_hash_12345678");
    auto p_loaded = blockfile.LoadBlock(fake_hash);

    ASSERT_NULL(p_loaded, "Should be null");

    CleanupDir(str_test_dir);
}

// ============================================================================
// BlockExists Tests
// ============================================================================

TEST(BlockFileBlockExists) {
    std::string str_test_dir = CreateTempDir();

    CBlockFile blockfile(str_test_dir);

    // Test non-existent block
    CHash fake_hash("fake_hash_123");
    ASSERT_FALSE(blockfile.BlockExists(fake_hash), "Assertion should be true");

    // Save a block
    auto p_block = CreateTestBlock(1, "miner123", 1);
    CHash hash = p_block->GetHash();
    ASSERT_TRUE(blockfile.SaveBlock(p_block), "Assertion should be true");

    // Now it should exist
    ASSERT_TRUE(blockfile.BlockExists(hash), "Assertion should be true");

    // Non-existent block still shouldn't exist
    ASSERT_FALSE(blockfile.BlockExists(fake_hash), "Assertion should be true");

    CleanupDir(str_test_dir);
}

// ============================================================================
// Duplicate Block Tests
// ============================================================================

TEST(BlockFileDuplicateSave) {
    std::string str_test_dir = CreateTempDir();

    CBlockFile blockfile(str_test_dir);

    // Create and save block
    auto p_block = CreateTestBlock(10, "miner", 2);
    CHash hash = p_block->GetHash();

    ASSERT_TRUE(blockfile.SaveBlock(p_block), "Assertion should be true");
    ASSERT_TRUE(blockfile.BlockExists(hash), "Assertion should be true");

    // Get the block file size after first save
    std::string str_block_file = str_test_dir + "/block_" +
                                 std::to_string(p_block->GetHeight()).insert(0, 10 - std::to_string(p_block->GetHeight()).length(), '0') +
                                 ".dat";
    std::ifstream ifs(str_block_file, std::ios::binary | std::ios::ate);
    size_t n_size_before = 0;
    if (ifs.is_open()) {
        n_size_before = ifs.tellg();
        ifs.close();
    }

    // Try to save same block again (should succeed but not duplicate)
    ASSERT_TRUE(blockfile.SaveBlock(p_block), "Assertion should be true");

    // Verify file size has not changed (no duplicate data written)
    std::ifstream ifs2(str_block_file, std::ios::binary | std::ios::ate);
    size_t n_size_after = 0;
    if (ifs2.is_open()) {
        n_size_after = ifs2.tellg();
        ifs2.close();
    }
    ASSERT_EQUAL(n_size_after, n_size_before, "Block file size should not change on duplicate save");

    // Should still be able to load it
    auto p_loaded = blockfile.LoadBlock(hash);
    ASSERT_NOT_NULL(p_loaded, "Should not be null");
    ASSERT_EQUAL(p_loaded->GetHeight(), 10, "Values should match");

    CleanupDir(str_test_dir);
}

// ============================================================================
// Index Persistence Tests
// ============================================================================

TEST(BlockFileIndexPersistence) {
    std::string str_test_dir = CreateTempDir();

    CHash hash1, hash2, hash3;

    // First session: Save blocks
    {
        CBlockFile blockfile1(str_test_dir);

        auto p_block1 = CreateTestBlock(0, "miner1", 1);
        auto p_block2 = CreateTestBlock(1, "miner2", 2);
        auto p_block3 = CreateTestBlock(2, "miner3", 3);

        hash1 = p_block1->GetHash();
        hash2 = p_block2->GetHash();
        hash3 = p_block3->GetHash();

        ASSERT_TRUE(blockfile1.SaveBlock(p_block1), "Assertion should be true");
        ASSERT_TRUE(blockfile1.SaveBlock(p_block2), "Assertion should be true");
        ASSERT_TRUE(blockfile1.SaveBlock(p_block3), "Assertion should be true");
    }  // blockfile1 destroyed, index should be saved

    // Second session: Load blocks (tests index loading)
    {
        CBlockFile blockfile2(str_test_dir);

        // All blocks should exist
        ASSERT_TRUE(blockfile2.BlockExists(hash1), "Assertion should be true");
        ASSERT_TRUE(blockfile2.BlockExists(hash2), "Assertion should be true");
        ASSERT_TRUE(blockfile2.BlockExists(hash3), "Assertion should be true");

        // Load and verify
        auto p_loaded1 = blockfile2.LoadBlock(hash1);
        auto p_loaded2 = blockfile2.LoadBlock(hash2);
        auto p_loaded3 = blockfile2.LoadBlock(hash3);

        ASSERT_NOT_NULL(p_loaded1, "Should not be null");
        ASSERT_NOT_NULL(p_loaded2, "Should not be null");
        ASSERT_NOT_NULL(p_loaded3, "Should not be null");

        ASSERT_EQUAL(p_loaded1->GetHeight(), 0, "Values should match");
        ASSERT_EQUAL(p_loaded2->GetHeight(), 1, "Values should match");
        ASSERT_EQUAL(p_loaded3->GetHeight(), 2, "Values should match");

        ASSERT_EQUAL(p_loaded1->GetTransactions().size(), 1, "Values should match");
        ASSERT_EQUAL(p_loaded2->GetTransactions().size(), 2, "Values should match");
        ASSERT_EQUAL(p_loaded3->GetTransactions().size(), 3, "Values should match");
    }

    CleanupDir(str_test_dir);
}

TEST(BlockFileIndexFileCreated) {
    std::string str_test_dir = CreateTempDir();

    {
        CBlockFile blockfile(str_test_dir);
        auto p_block = CreateTestBlock(0, "miner", 1);
        ASSERT_TRUE(blockfile.SaveBlock(p_block), "Assertion should be true");
    }  // Destructor should save index

    // Check that index file exists
    std::string str_index_path = str_test_dir + "/block_index.dat";
    ASSERT_TRUE(FileExists(str_index_path), "Assertion should be true");

    CleanupDir(str_test_dir);
}

// ============================================================================
// GetGenesisBlock Tests
// ============================================================================

TEST(BlockFileGetGenesisBlock) {
    std::string str_test_dir = CreateTempDir();

    CBlockFile blockfile(str_test_dir);

    // Save blocks in random order, including genesis
    auto p_genesis = CreateTestBlock(0, "genesis_miner", 1);
    auto p_block1 = CreateTestBlock(1, "miner1", 2);
    auto p_block2 = CreateTestBlock(2, "miner2", 3);

    // Save out of order
    ASSERT_TRUE(blockfile.SaveBlock(p_block2), "Assertion should be true");
    ASSERT_TRUE(blockfile.SaveBlock(p_genesis), "Assertion should be true");
    ASSERT_TRUE(blockfile.SaveBlock(p_block1), "Assertion should be true");

    // Should find genesis (height 0)
    auto p_found_genesis = blockfile.GetGenesisBlock();
    ASSERT_NOT_NULL(p_found_genesis, "Should not be null");
    ASSERT_EQUAL(p_found_genesis->GetHeight(), 0, "Values should match");
    ASSERT_EQUAL(p_found_genesis->GetMiner(), "genesis_miner", "Values should match");
    ASSERT_EQUAL(p_found_genesis->GetTransactions().size(), 1, "Values should match");

    CleanupDir(str_test_dir);
}

TEST(BlockFileGetGenesisBlockNotFound) {
    std::string str_test_dir = CreateTempDir();

    CBlockFile blockfile(str_test_dir);

    // Save only non-genesis blocks
    auto p_block1 = CreateTestBlock(1, "miner1", 1);
    auto p_block2 = CreateTestBlock(2, "miner2", 1);

    ASSERT_TRUE(blockfile.SaveBlock(p_block1), "Assertion should be true");
    ASSERT_TRUE(blockfile.SaveBlock(p_block2), "Assertion should be true");

    // Should not find genesis
    auto p_genesis = blockfile.GetGenesisBlock();
    ASSERT_NULL(p_genesis, "Should be null");

    CleanupDir(str_test_dir);
}

// ============================================================================
// Data Integrity Tests
// ============================================================================

TEST(BlockFileLargeTransactionData) {
    std::string str_test_dir = CreateTempDir();

    CBlockFile blockfile(str_test_dir);

    // Create block with large transaction data
    auto p_block = std::make_shared<CBlock>(CHash(), 0, "miner");

    // Create 1MB of data
    std::vector<uint8_t> large_data(1024 * 1024);
    for (size_t i = 0; i < large_data.size(); i++) {
        large_data[i] = static_cast<uint8_t>(i % 256);
    }

    auto p_tx = std::make_shared<CTransaction>("owner", "target", large_data, 1000);
    p_block->AddTransaction(p_tx);

    CHash hash = p_block->GetHash();

    // Save and load
    ASSERT_TRUE(blockfile.SaveBlock(p_block), "Assertion should be true");
    auto p_loaded = blockfile.LoadBlock(hash);

    ASSERT_NOT_NULL(p_loaded, "Should not be null");
    ASSERT_EQUAL(p_loaded->GetTransactions().size(), 1, "Values should match");

    // Verify data integrity
    const auto& loaded_data = p_loaded->GetTransactions()[0]->m_data;
    ASSERT_EQUAL(loaded_data.size(), large_data.size(), "Values should match");

    // Verify first, middle, and last bytes
    ASSERT_EQUAL(loaded_data[0], large_data[0], "Values should match");
    ASSERT_EQUAL(loaded_data[512 * 1024], large_data[512 * 1024], "Values should match");
    ASSERT_EQUAL(loaded_data[1024 * 1024 - 1], large_data[1024 * 1024 - 1], "Values should match");

    CleanupDir(str_test_dir);
}

TEST(BlockFileEmptyStringFields) {
    std::string str_test_dir = CreateTempDir();

    CBlockFile blockfile(str_test_dir);

    // Create transaction with empty strings
    auto p_block = std::make_shared<CBlock>(CHash(), 0, "");
    auto p_tx = std::make_shared<CTransaction>("", "", std::vector<uint8_t>(), 0);
    p_block->AddTransaction(p_tx);

    CHash hash = p_block->GetHash();

    // Save and load
    ASSERT_TRUE(blockfile.SaveBlock(p_block), "Assertion should be true");
    auto p_loaded = blockfile.LoadBlock(hash);

    ASSERT_NOT_NULL(p_loaded, "Should not be null");
    ASSERT_EQUAL(p_loaded->GetMiner(), "", "Values should match");
    ASSERT_EQUAL(p_loaded->GetTransactions()[0]->m_str_owner, "", "Values should match");
    ASSERT_EQUAL(p_loaded->GetTransactions()[0]->m_str_target, "", "Values should match");

    CleanupDir(str_test_dir);
}

TEST(BlockFileManyTransactions) {
    std::string str_test_dir = CreateTempDir();

    CBlockFile blockfile(str_test_dir);

    // Create block with 100 transactions
    auto p_block = CreateTestBlock(0, "miner", 100);
    CHash hash = p_block->GetHash();

    // Save and load
    ASSERT_TRUE(blockfile.SaveBlock(p_block), "Assertion should be true");
    auto p_loaded = blockfile.LoadBlock(hash);

    ASSERT_NOT_NULL(p_loaded, "Should not be null");
    ASSERT_EQUAL(p_loaded->GetTransactions().size(), 100, "Values should match");

    // Verify all transactions
    for (size_t i = 0; i < 100; i++) {
        ASSERT_EQUAL(p_loaded->GetTransactions()[i]->m_str_owner,
                     "owner_" + std::to_string(i), "Owner should match");
        ASSERT_EQUAL(p_loaded->GetTransactions()[i]->m_n_reward,
                     100 + i * 10, "Reward should match");
    }

    CleanupDir(str_test_dir);
}

// ============================================================================
// File System Tests
// ============================================================================

TEST(BlockFileDataDirectoryCreated) {
    std::string str_test_dir = CreateTempDir();

    // Remove directory to test creation
    CleanupDir(str_test_dir);
    ASSERT_FALSE(FileExists(str_test_dir + "/blk00000.dat"), "File should not exist");

    // Create blockfile (should create directory)
    CBlockFile blockfile(str_test_dir);

    // Save a block (should create blk00000.dat)
    auto p_block = CreateTestBlock(0, "miner", 1);
    ASSERT_TRUE(blockfile.SaveBlock(p_block), "Assertion should be true");

    // Check that data file was created
    ASSERT_TRUE(FileExists(str_test_dir + "/blk00000.dat"), "File should exist");

    CleanupDir(str_test_dir);
}

TEST(BlockFileMultipleInstances) {
    std::string str_test_dir = CreateTempDir();

    CHash hash1, hash2;

    // First instance saves block
    {
        CBlockFile blockfile1(str_test_dir);
        auto p_block1 = CreateTestBlock(0, "miner1", 1);
        hash1 = p_block1->GetHash();
        ASSERT_TRUE(blockfile1.SaveBlock(p_block1), "Assertion should be true");
    }

    // Second instance saves different block
    {
        CBlockFile blockfile2(str_test_dir);

        // Should be able to see first block
        ASSERT_TRUE(blockfile2.BlockExists(hash1), "Assertion should be true");

        // Save second block
        auto p_block2 = CreateTestBlock(1, "miner2", 1);
        hash2 = p_block2->GetHash();
        ASSERT_TRUE(blockfile2.SaveBlock(p_block2), "Assertion should be true");
    }

    // Third instance verifies both blocks
    {
        CBlockFile blockfile3(str_test_dir);
        ASSERT_TRUE(blockfile3.BlockExists(hash1), "Assertion should be true");
        ASSERT_TRUE(blockfile3.BlockExists(hash2), "Assertion should be true");

        auto p_loaded1 = blockfile3.LoadBlock(hash1);
        auto p_loaded2 = blockfile3.LoadBlock(hash2);

        ASSERT_NOT_NULL(p_loaded1, "Should not be null");
        ASSERT_NOT_NULL(p_loaded2, "Should not be null");
        ASSERT_EQUAL(p_loaded1->GetHeight(), 0, "Values should match");
        ASSERT_EQUAL(p_loaded2->GetHeight(), 1, "Values should match");
    }

    CleanupDir(str_test_dir);
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST(BlockFileZeroRewardTransaction) {
    std::string str_test_dir = CreateTempDir();

    CBlockFile blockfile(str_test_dir);

    auto p_block = std::make_shared<CBlock>(CHash(), 0, "miner");
    auto p_tx = std::make_shared<CTransaction>("owner", "target",
                                                std::vector<uint8_t>{1, 2, 3}, 0);
    p_block->AddTransaction(p_tx);

    CHash hash = p_block->GetHash();

    ASSERT_TRUE(blockfile.SaveBlock(p_block), "Assertion should be true");
    auto p_loaded = blockfile.LoadBlock(hash);

    ASSERT_NOT_NULL(p_loaded, "Should not be null");
    ASSERT_EQUAL(p_loaded->GetTransactions()[0]->m_n_reward, 0, "Values should match");

    CleanupDir(str_test_dir);
}

TEST(BlockFileSpecialCharactersInStrings) {
    std::string str_test_dir = CreateTempDir();

    CBlockFile blockfile(str_test_dir);

    // Test special characters in miner and transaction fields
    std::string str_special = "test@#$%^&*()_+-=[]{}|;:',.<>?/~`";

    auto p_block = std::make_shared<CBlock>(CHash(), 0, str_special);
    auto p_tx = std::make_shared<CTransaction>(str_special, str_special,
                                                std::vector<uint8_t>{1}, 100);
    p_block->AddTransaction(p_tx);

    CHash hash = p_block->GetHash();

    ASSERT_TRUE(blockfile.SaveBlock(p_block), "Assertion should be true");
    auto p_loaded = blockfile.LoadBlock(hash);

    ASSERT_NOT_NULL(p_loaded, "Should not be null");
    ASSERT_EQUAL(p_loaded->GetMiner(), str_special, "Values should match");
    ASSERT_EQUAL(p_loaded->GetTransactions()[0]->m_str_owner, str_special, "Values should match");
    ASSERT_EQUAL(p_loaded->GetTransactions()[0]->m_str_target, str_special, "Values should match");

    CleanupDir(str_test_dir);
}
