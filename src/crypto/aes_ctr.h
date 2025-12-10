#ifndef AES_CTR_H
#define AES_CTR_H

#include <array>
#include <cstdint>
#include <vector>

/**
 * CAesCtr - AES-128-CTR encryption/decryption
 *
 * Counter (CTR) mode is a stream cipher mode that converts AES into a stream cipher.
 * No padding is required, and encryption/decryption are the same operation.
 *
 * Used by Ethereum Web3 keystore format for private key encryption.
 *
 * Usage:
 *   std::array<uint8_t, 16> key = ...;  // AES-128 key
 *   std::array<uint8_t, 16> iv = CAesCtr::GenerateIV();
 *   std::vector<uint8_t> ciphertext = CAesCtr::Encrypt(plaintext, key, iv);
 *   std::vector<uint8_t> plaintext2 = CAesCtr::Decrypt(ciphertext, key, iv);
 */
class CAesCtr {
public:
    // AES-128 key size in bytes
    static constexpr size_t KEY_SIZE = 16;

    // Initialization vector (IV) size in bytes
    static constexpr size_t IV_SIZE = 16;

    /**
     * Encrypt data using AES-128-CTR
     *
     * @param data - Plaintext data to encrypt
     * @param n_len - Length of plaintext data
     * @param key - AES-128 key (16 bytes)
     * @param iv - Initialization vector (16 bytes)
     * @return Ciphertext (same length as plaintext)
     * @throws std::runtime_error if encryption fails
     */
    static std::vector<uint8_t> Encrypt(
        const uint8_t* data, size_t n_len,
        const std::array<uint8_t, KEY_SIZE>& key,
        const std::array<uint8_t, IV_SIZE>& iv
    );

    /**
     * Decrypt data using AES-128-CTR
     *
     * @param data - Ciphertext data to decrypt
     * @param n_len - Length of ciphertext data
     * @param key - AES-128 key (16 bytes)
     * @param iv - Initialization vector (16 bytes)
     * @return Plaintext (same length as ciphertext)
     * @throws std::runtime_error if decryption fails
     */
    static std::vector<uint8_t> Decrypt(
        const uint8_t* data, size_t n_len,
        const std::array<uint8_t, KEY_SIZE>& key,
        const std::array<uint8_t, IV_SIZE>& iv
    );

    /**
     * Generate random initialization vector (IV) using OpenSSL RAND_bytes
     *
     * @return Random IV (16 bytes)
     * @throws std::runtime_error if random generation fails
     */
    static std::array<uint8_t, IV_SIZE> GenerateIV();
};

#endif // AES_CTR_H
