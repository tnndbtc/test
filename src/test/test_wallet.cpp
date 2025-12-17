// ============= test_wallet.cpp =============
#include "unit_test.h"
#include "wallet/wallet.h"

using namespace UnitTest;

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
    // Note: m_str_owner, m_str_target, and m_n_reward fields have been removed
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
    // Note: m_n_reward field has been removed
}
