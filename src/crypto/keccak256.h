#ifndef KECCAK256_H
#define KECCAK256_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

/**
 * CKeccak256 - Keccak-256 hash function (Ethereum standard)
 *
 * IMPORTANT: This is Keccak-256 (original Keccak), NOT NIST SHA3-256.
 * Ethereum uses the original Keccak-256 specification.
 *
 * Usage:
 *   std::array<uint8_t, 32> hash = CKeccak256::Hash(data, length);
 *   std::string hex = CKeccak256::ToHex(hash);
 */
class CKeccak256 {
public:
    // Hash size in bytes
    static constexpr size_t HASH_SIZE = 32;

    // Compute Keccak-256 hash from raw data
    static std::array<uint8_t, HASH_SIZE> Hash(const uint8_t* data, size_t n_len);

    // Compute Keccak-256 hash from vector
    static std::array<uint8_t, HASH_SIZE> Hash(const std::vector<uint8_t>& data);

    // Compute Keccak-256 hash from string
    static std::array<uint8_t, HASH_SIZE> Hash(const std::string& str);

    // Convert hash to lowercase hex string (no 0x prefix)
    static std::string ToHex(const std::array<uint8_t, HASH_SIZE>& hash);

    // Convert hex string to hash array
    static std::array<uint8_t, HASH_SIZE> FromHex(const std::string& str_hex);

private:
    // Helper: Convert byte to 2-char hex string
    static std::string ByteToHex(uint8_t byte);

    // Helper: Convert 2-char hex string to byte
    static uint8_t HexToByte(const std::string& str_hex);
};

#endif // KECCAK256_H
