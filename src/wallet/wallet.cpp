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
    (void)str_target; // Unused parameter for now

    // Create transaction using default constructor
    auto p_tx = std::make_shared<CTransaction>();

    // Populate transaction fields
    p_tx->m_n_version = TxVersion::V0;
    p_tx->m_n_nonce = 0;

    // Hash wallet address to create 20-byte from address
    std::string str_address = GetAddress();
    CHash address_hash = CHash::ComputeSHA256(str_address);
    auto hash_bytes = address_hash.GetBytes();
    p_tx->m_from.assign(hash_bytes.begin(), hash_bytes.begin() + std::min<size_t>(20, hash_bytes.size()));
    while (p_tx->m_from.size() < 20) {
        p_tx->m_from.push_back(0);
    }

    p_tx->m_type = TransactionType::TRANSFER;
    p_tx->m_data.assign(data.begin(), data.end());
    p_tx->m_n_fee = n_reward;
    // m_signature is empty initially
    p_tx->m_n_data_size = data.size();
    // m_str_metadata is empty

    // Compute transaction ID
    std::string str_id_input;
    str_id_input += std::to_string(static_cast<uint8_t>(p_tx->m_n_version));
    str_id_input += std::to_string(p_tx->m_n_nonce);
    str_id_input.insert(str_id_input.end(), p_tx->m_from.begin(), p_tx->m_from.end());
    str_id_input += std::to_string(static_cast<uint8_t>(p_tx->m_type));
    str_id_input.insert(str_id_input.end(), p_tx->m_data.begin(), p_tx->m_data.end());
    str_id_input += std::to_string(p_tx->m_n_fee);
    str_id_input.insert(str_id_input.end(), p_tx->m_signature.begin(), p_tx->m_signature.end());
    p_tx->m_id = CHash::ComputeSHA256(str_id_input);

    return p_tx;
}

