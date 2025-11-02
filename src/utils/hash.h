// ============= hash.h =============
#ifndef HASH_H
#define HASH_H

#include <string>
#include <array>
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
 * - Automatic SHA-256 computation on construction
 * - Comparison operators for equality and ordering
 * - Immutable hash value after construction
 * - Binary storage for efficient network transmission
 *
 * Example usage:
 *   CHash hash1("Hello, World!");
 *   CHash hash2("Hello, World!");
 *   if (hash1 == hash2) {
 *       std::cout << "Hashes match: " << hash1.GetData() << std::endl;
 *   }
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
     * @brief Construct hash from input string
     * @param input String to hash using SHA-256
     *
     * Computes SHA-256 hash of input and stores as binary data.
     * Uses OpenSSL SHA256 function for cryptographic hashing.
     */
    explicit CHash(const std::string& input);

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

#endif
