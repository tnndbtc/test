// ============= hash.h =============
#ifndef HASH_H
#define HASH_H

#include <string>

/**
 * @class CHash
 * @brief SHA-256 cryptographic hash wrapper
 *
 * Encapsulates SHA-256 hash computation and storage using OpenSSL.
 * Provides a convenient interface for hashing strings and comparing
 * hash values. Hash data is stored as a 64-character hexadecimal string.
 *
 * Features:
 * - Automatic SHA-256 computation on construction
 * - Comparison operators for equality and ordering
 * - Immutable hash value after construction
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
    std::string m_str_data;  ///< Hexadecimal string representation of hash (64 chars)

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
     * Computes SHA-256 hash of input and stores as hex string.
     * Uses OpenSSL SHA256 function for cryptographic hashing.
     */
    explicit CHash(const std::string& input);

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
     * @return Const reference to 64-character hex string
     *
     * Returns the SHA-256 hash as a lowercase hexadecimal string.
     * Each byte of the 32-byte hash is represented as two hex digits.
     */
    const std::string& GetData() const { return m_str_data; }
};

#endif
