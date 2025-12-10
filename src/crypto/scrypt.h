#ifndef SCRYPT_H
#define SCRYPT_H

#include <array>
#include <cstdint>
#include <string>

/**
 * CScrypt - Scrypt key derivation function (KDF)
 *
 * Uses Ethereum Web3 standard parameters for keystore encryption.
 *
 * Parameters:
 *   dklen = 32 bytes (derived key length)
 *   N = 262144 (CPU/memory cost parameter, 2^18)
 *   r = 8 (block size parameter)
 *   p = 1 (parallelization parameter)
 *
 * Usage:
 *   std::array<uint8_t, 32> salt = CScrypt::GenerateSalt();
 *   std::array<uint8_t, 32> key = CScrypt::DeriveKey("password", salt);
 */
class CScrypt {
public:
    // Ethereum Web3 standard parameters
    static constexpr size_t DKLEN = 32;        // Derived key length in bytes
    static constexpr uint64_t N = 262144;      // CPU/memory cost parameter (2^18)
    static constexpr uint32_t R = 8;           // Block size parameter
    static constexpr uint32_t P = 1;           // Parallelization parameter

    // Salt size in bytes
    static constexpr size_t SALT_SIZE = 32;

    /**
     * Derive encryption key from password using scrypt KDF
     *
     * @param str_password - User password
     * @param salt - Random salt (32 bytes)
     * @return Derived key (32 bytes)
     * @throws std::runtime_error if scrypt fails
     */
    static std::array<uint8_t, DKLEN> DeriveKey(
        const std::string& str_password,
        const std::array<uint8_t, SALT_SIZE>& salt
    );

    /**
     * Generate random salt using OpenSSL RAND_bytes
     *
     * @return Random salt (32 bytes)
     * @throws std::runtime_error if random generation fails
     */
    static std::array<uint8_t, SALT_SIZE> GenerateSalt();
};

#endif // SCRYPT_H
