// ============= wallet.cpp =============
#include "wallet.h"
#include <stdexcept>

// Create new wallet with random keypair
CWallet::CWallet() {
    // Generate new random keypair using secp256k1
    m_p_keypair = std::make_unique<CKeyPair>();
}

// Load wallet from keystore file
CWallet::CWallet(const std::string& str_keystore_path, const std::string& str_password) {
    // Load keystore from file
    CKeystore keystore = CKeystore::LoadFromFile(str_keystore_path);

    // Decrypt private key
    std::array<uint8_t, 32> private_key = keystore.DecryptPrivateKey(str_password);

    // Create keypair from private key
    m_p_keypair = std::make_unique<CKeyPair>(private_key);
    m_str_keystore_path = str_keystore_path;

    // Verify address matches
    if (m_p_keypair->GetAddress() != keystore.GetAddress()) {
        throw std::runtime_error("Address mismatch after decryption");
    }
}

// Save wallet to keystore file
void CWallet::SaveToKeystore(const std::string& str_keystore_path, const std::string& str_password) {
    // Create keystore from keypair
    CKeystore keystore(*m_p_keypair, str_password);

    // Save to file
    keystore.SaveToFile(str_keystore_path);
    m_str_keystore_path = str_keystore_path;
}

// Get Ethereum address
std::string CWallet::GetAddress() const {
    return m_p_keypair->GetAddress();
}

// Create transaction
std::shared_ptr<CTransaction> CWallet::CreateTransaction(
    const std::string& str_target,
    const std::vector<uint8_t>& data,
    uint64_t n_reward
) {
    return std::make_shared<CTransaction>(GetAddress(), str_target, data, n_reward);
}

