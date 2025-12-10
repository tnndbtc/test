// ============= wallet.h =============
#ifndef WALLET_H
#define WALLET_H

#include "blockcore/transaction.h"
#include "crypto/keypair.h"
#include "crypto/keystore.h"
#include <string>
#include <memory>
#include <vector>
#include <cstdint>

class CWallet {
private:
    std::unique_ptr<CKeyPair> m_p_keypair;  // Keypair (private/public keys + address)
    std::string m_str_keystore_path;        // Path to keystore file

public:
    /**
     * Create new wallet with random keypair
     */
    CWallet();

    /**
     * Load wallet from keystore file
     * @param str_keystore_path - Path to keystore JSON file
     * @param str_password - Password for decryption
     * @throws std::runtime_error if load or decryption fails
     */
    explicit CWallet(const std::string& str_keystore_path, const std::string& str_password);

    /**
     * Save wallet to keystore file
     * @param str_keystore_path - Path to save keystore
     * @param str_password - Password for encryption
     * @throws std::runtime_error if save fails
     */
    void SaveToKeystore(const std::string& str_keystore_path, const std::string& str_password);

    /**
     * Get Ethereum address
     */
    std::string GetAddress() const;

    /**
     * Create transaction (will need signing in future)
     */
    std::shared_ptr<CTransaction> CreateTransaction(
        const std::string& str_target,
        const std::vector<uint8_t>& data,
        uint64_t n_reward = 0
    );

    /**
     * Get keypair (for signing transactions in future)
     */
    const CKeyPair& GetKeyPair() const { return *m_p_keypair; }
};

#endif

