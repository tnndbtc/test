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
#include <algorithm>
#include <openssl/sha.h>

/**
 * @brief Default constructor - initialize to zero hash
 *
 * Creates a hash filled with zeros (32 zero bytes).
 * Used for placeholder or uninitialized hash values.
 */
CHash::CHash() {
    m_data.fill(0);
}

/**
 * @brief Construct hash by computing SHA-256 of input
 * @param input String to hash
 *
 * Computes SHA-256 hash using OpenSSL and stores the 32-byte
 * binary output directly in m_data array.
 */
CHash::CHash(const std::string& input) {
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()),
           input.length(),
           m_data.data());
}

/**
 * @brief Construct hash from binary data
 * @param data Pointer to 32-byte binary hash data
 * @param size Size of data (must be 32)
 *
 * Copies binary hash data directly into internal storage.
 * If size != 32, fills remaining bytes with zeros.
 */
CHash::CHash(const unsigned char* data, size_t size) {
    if (size == 32) {
        std::copy(data, data + 32, m_data.begin());
    } else {
        // Handle incorrect size - fill with zeros
        m_data.fill(0);
        if (size > 0 && size < 32) {
            std::copy(data, data + size, m_data.begin());
        }
    }
}

/**
 * @brief Get hash data as hexadecimal string
 * @return 64-character hex string
 *
 * Converts binary hash data to hexadecimal string on-demand.
 * Each byte is represented as two hex digits (00-ff).
 */
std::string CHash::GetData() const {
    std::stringstream ss;
    for(size_t n_i = 0; n_i < m_data.size(); n_i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(m_data[n_i]);
    }
    return ss.str();
}

/**
 * @brief Compare two hashes for equality
 * @param other Hash to compare with
 * @return true if hashes are identical
 *
 * Performs binary comparison of hash values.
 */
bool CHash::operator==(const CHash& other) const {
    return m_data == other.m_data;
}

/**
 * @brief Compare two hashes lexicographically
 * @param other Hash to compare with
 * @return true if this hash is less than other
 *
 * Enables sorting and use in ordered containers (std::map, std::set).
 * Uses lexicographic comparison of binary data.
 */
bool CHash::operator<(const CHash& other) const {
    return m_data < other.m_data;
}
