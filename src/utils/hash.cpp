// ============= hash.cpp =============
/**
 * @file hash.cpp
 * @brief Implementation of SHA-256 cryptographic hash wrapper
 *
 * Provides SHA-256 hash computation using OpenSSL library.
 * Converts binary hash output to hexadecimal string representation.
 */

#include "hash.h"
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>

/**
 * @brief Default constructor - initialize to zero hash
 *
 * Creates a hash filled with zeros (64 '0' characters).
 * Used for placeholder or uninitialized hash values.
 */
CHash::CHash() : m_str_data(64, '0') {}

/**
 * @brief Construct hash by computing SHA-256 of input
 * @param input String to hash
 *
 * Computes SHA-256 hash using OpenSSL and converts the 32-byte
 * binary output to a 64-character lowercase hexadecimal string.
 * Each byte is represented as two hex digits (00-ff).
 */
CHash::CHash(const std::string& input) {
    unsigned char hash_buffer[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.length(), hash_buffer);

    std::stringstream ss;
    for(int n_i = 0; n_i < SHA256_DIGEST_LENGTH; n_i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash_buffer[n_i]);
    }
    m_str_data = ss.str();
}

/**
 * @brief Compare two hashes for equality
 * @param other Hash to compare with
 * @return true if hashes are identical
 *
 * Performs string comparison of hexadecimal hash values.
 */
bool CHash::operator==(const CHash& other) const {
    return m_str_data == other.m_str_data;
}

/**
 * @brief Compare two hashes lexicographically
 * @param other Hash to compare with
 * @return true if this hash is less than other
 *
 * Enables sorting and use in ordered containers (std::map, std::set).
 * Uses lexicographic comparison of hex strings.
 */
bool CHash::operator<(const CHash& other) const {
    return m_str_data < other.m_str_data;
}
