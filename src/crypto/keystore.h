#ifndef KEYSTORE_H
#define KEYSTORE_H

#include "keypair.h"
#include "../utils/json.hpp"
#include <string>
#include <array>
#include <vector>
#include <cstdint>

/**
 * CKeystore - Ethereum Web3 keystore format (Version 3)
 *
 * Encrypts private keys using:
 *   - scrypt KDF for key derivation
 *   - AES-128-CTR for encryption
 *   - keccak256 MAC for integrity verification
 *
 * JSON Format:
 * {
 *   "version": 3,
 *   "id": "uuid",
 *   "address": "hex_address",
 *   "crypto": {
 *     "ciphertext": "hex",
 *     "cipherparams": {"iv": "hex"},
 *     "cipher": "aes-128-ctr",
 *     "kdf": "scrypt",
 *     "kdfparams": {"dklen": 32, "salt": "hex", "n": 262144, "r": 8, "p": 1},
 *     "mac": "hex"
 *   }
 * }
 *
 * Usage:
 *   // Create keystore from keypair
 *   CKeyPair keypair;
 *   CKeystore keystore(keypair, "password");
 *   keystore.SaveToFile("wallet.json");
 *
 *   // Load and decrypt
 *   CKeystore loaded = CKeystore::LoadFromFile("wallet.json");
 *   std::array<uint8_t, 32> private_key = loaded.DecryptPrivateKey("password");
 */
class CKeystore {
private:
    int n_version;                          // Always 3 for Web3
    std::string str_id;                     // UUID v4
    std::string str_address;                // Ethereum address (40-char hex)
    std::vector<uint8_t> ciphertext;        // Encrypted private key
    std::array<uint8_t, 16> iv;             // AES IV
    std::string str_cipher;                 // "aes-128-ctr"
    std::string str_kdf;                    // "scrypt"
    std::array<uint8_t, 32> salt;           // Scrypt salt
    std::array<uint8_t, 32> mac;            // MAC for integrity check

public:
    /**
     * Create new keystore from keypair and password
     * @param keypair - The keypair to encrypt
     * @param str_password - Password for encryption
     * @throws std::runtime_error if encryption fails
     */
    CKeystore(const CKeyPair& keypair, const std::string& str_password);

    /**
     * Default constructor (for loading from file)
     */
    CKeystore() : n_version(3), str_cipher("aes-128-ctr"), str_kdf("scrypt") {}

    /**
     * Load keystore from JSON file
     * @param str_filepath - Path to keystore file
     * @return CKeystore object
     * @throws std::runtime_error if file not found or invalid JSON
     */
    static CKeystore LoadFromFile(const std::string& str_filepath);

    /**
     * Save keystore to JSON file
     * @param str_filepath - Path to save keystore
     * @throws std::runtime_error if file cannot be written
     */
    void SaveToFile(const std::string& str_filepath) const;

    /**
     * Decrypt and recover private key
     * @param str_password - Password for decryption
     * @return Private key (32 bytes)
     * @throws std::runtime_error if wrong password or corrupted keystore
     */
    std::array<uint8_t, 32> DecryptPrivateKey(const std::string& str_password) const;

    /**
     * Get address (without decrypting)
     * @return Ethereum address (40-char hex)
     */
    std::string GetAddress() const { return str_address; }

    /**
     * Export to JSON object
     * @return nlohmann::json object
     */
    nlohmann::json ToJson() const;

    /**
     * Import from JSON object
     * @param json_obj - JSON object
     * @return CKeystore object
     * @throws std::runtime_error if invalid JSON structure
     */
    static CKeystore FromJson(const nlohmann::json& json_obj);

private:
    /**
     * Generate UUID v4
     * @return UUID string (e.g., "550e8400-e29b-41d4-a716-446655440000")
     */
    static std::string GenerateUUID();

    /**
     * Verify MAC
     * @param str_password - Password to verify
     * @return true if MAC is valid, false otherwise
     */
    bool VerifyMAC(const std::string& str_password) const;

    /**
     * Compute MAC: keccak256(derived_key[16:32] + ciphertext)
     * @param mac_key - Last 16 bytes of derived key
     * @param ciphertext - Encrypted data
     * @return MAC (32 bytes)
     */
    static std::array<uint8_t, 32> ComputeMAC(
        const std::array<uint8_t, 16>& mac_key,
        const std::vector<uint8_t>& ciphertext
    );
};

#endif // KEYSTORE_H
