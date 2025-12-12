// ============= hash.h =============
#ifndef HASH_H
#define HASH_H

#include <string>
#include <array>
#include <vector>
#include <cstdint>

/**
 * @class CHash
 * @brief SHA-256 cryptographic hash wrapper
 *
 * Encapsulates SHA-256 hash computation and storage using OpenSSL.
 * Provides a convenient interface for hashing strings and comparing
 * hash values. Hash data is stored as a 32-byte binary array internally.
 *
 * Features:
 * - SHA-256 computation via ComputeSHA256() static method
 * - Hex string parsing via constructor
 * - Comparison operators for equality and ordering
 * - Immutable hash value after construction
 * - Binary storage for efficient network transmission
 *
 * Example usage:
 *   // Compute new hash
 *   CHash hash1 = CHash::ComputeSHA256("Hello, World!");
 *   CHash hash2 = CHash::ComputeSHA256("Hello, World!");
 *   if (hash1 == hash2) {
 *       std::cout << "Hashes match: " << hash1.GetData() << std::endl;
 *   }
 *
 *   // Parse hex string
 *   CHash hash3("a1b2c3d4e5f6...");  // 64-character hex string
 */
class CHash {
private:
    std::array<uint8_t, 32> m_data;  ///< Binary hash data (32 bytes, SHA-256 output)

public:
    /**
     * @brief Default constructor - creates hash of empty string
     *
     * Initializes hash to all zeros (64 '0' characters).
     */
    CHash();

    /**
     * @brief Compute SHA-256 hash of input string
     * @param input String to hash
     * @return CHash object containing the SHA-256 hash
     *
     * Example: CHash hash = CHash::ComputeSHA256("hello world");
     * This computes the SHA-256 hash of the input string.
     */
    static CHash ComputeSHA256(const std::string& input);

    /**
     * @brief Construct hash from 64-character hexadecimal string
     * @param str_hex Hex string representing the hash (must be exactly 64 characters)
     * @throws std::invalid_argument if hex string is invalid (wrong length or invalid characters)
     *
     * Example: CHash("a1b2c3d4...") parses the hex string into binary hash.
     * Note: This does NOT compute a hash - it parses an existing hash in hex format.
     */
    explicit CHash(const std::string& str_hex);

    /**
     * @brief Construct hash from binary data
     * @param data Pointer to 32-byte binary hash data
     * @param size Size of data (must be 32)
     *
     * Creates hash from pre-computed binary data (e.g., received from network).
     * Used for deserializing hashes without recomputing them.
     */
    CHash(const unsigned char* data, size_t size);

    /**
     * @brief Equality comparison operator
     * @param other Hash to compare with
     * @return true if hash values are identical, false otherwise
     */
    bool operator==(const CHash& other) const;

    /**
     * @brief Less-than comparison operator
     * @param other Hash to compare with
     * @return true if this hash is lexicographically less than other
     *
     * Enables use in ordered containers like std::map and std::set.
     */
    bool operator<(const CHash& other) const;

    /**
     * @brief Get hash data as hexadecimal string
     * @return 64-character hex string representation
     *
     * Converts the 32-byte binary hash to a lowercase hexadecimal string.
     * Each byte is represented as two hex digits (00-ff).
     * This method performs conversion on-demand for display purposes.
     */
    std::string GetData() const;

    /**
     * @brief Get raw binary hash data
     * @return Const reference to 32-byte array
     *
     * Returns direct access to the internal binary hash representation.
     * Use this for efficient network transmission and storage operations.
     */
    const std::array<uint8_t, 32>& GetBytes() const { return m_data; }
};

/**
 * @brief Convert bytes to hex string
 * @param data Binary data to convert
 * @return Hex string representation
 *
 * Converts a vector of bytes to a hexadecimal string.
 * Each byte is represented as two hex digits (00-ff).
 * Used for converting binary data to JSON-safe strings.
 */
std::string BytesToHex(const std::vector<uint8_t>& data);

/**
 * @brief Convert hex string to bytes
 * @param str_hex Hexadecimal string (must have even length)
 * @return Vector of bytes
 * @throws std::invalid_argument if hex string has invalid characters or odd length
 *
 * Converts a hexadecimal string to a vector of bytes.
 * Each pair of hex digits is converted to one byte.
 * Used for parsing hex strings from JSON, configuration, etc.
 */
std::vector<uint8_t> HexToBytes(const std::string& str_hex);

#endif
