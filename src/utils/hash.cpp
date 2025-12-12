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
 * @brief Compute SHA-256 hash of input string
 * @param input String to hash
 * @return CHash object containing the SHA-256 hash
 */
CHash CHash::ComputeSHA256(const std::string& input) {
    unsigned char hash_data[32];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()),
           input.length(),
           hash_data);
    return CHash(hash_data, 32);
}

/**
 * @brief Construct hash from 64-character hexadecimal string
 * @param str_hex Hex string representing the hash (must be exactly 64 characters)
 * @throws std::invalid_argument if hex string is invalid (wrong length or invalid characters)
 *
 * Parses a 64-character hex string into binary hash data.
 * Uses HexToBytes() utility function for conversion.
 */
CHash::CHash(const std::string& str_hex) {
    // Validate length (SHA-256 is 32 bytes = 64 hex characters)
    if (str_hex.size() != 64) {
        throw std::invalid_argument("Hex string must be exactly 64 characters (got " +
                                    std::to_string(str_hex.size()) + ")");
    }

    // Convert hex string to binary using HexToBytes utility
    std::vector<uint8_t> bytes = HexToBytes(str_hex);

    // Copy to fixed-size array
    std::copy(bytes.begin(), bytes.end(), m_data.begin());
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

/**
 * @brief Convert bytes to hex string
 * @param data Binary data to convert
 * @return Hex string representation
 *
 * Converts a vector of bytes to a hexadecimal string.
 * Each byte is represented as two hex digits (00-ff).
 */
std::string BytesToHex(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    for (uint8_t byte : data) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(byte);
    }
    return oss.str();
}

/**
 * @brief Convert hex string to bytes
 * @param str_hex Hexadecimal string
 * @return Vector of bytes
 * @throws std::invalid_argument if hex string has invalid format
 *
 * Converts a hexadecimal string to bytes.
 * Each pair of hex digits is converted to one byte.
 */
std::vector<uint8_t> HexToBytes(const std::string& str_hex) {
    // Validate length (must be even)
    if (str_hex.size() % 2 != 0) {
        throw std::invalid_argument("Hex string must have even length (got " +
                                    std::to_string(str_hex.size()) + ")");
    }

    std::vector<uint8_t> bytes;
    bytes.reserve(str_hex.size() / 2);

    // Convert hex string to binary
    for (size_t i = 0; i < str_hex.size(); i += 2) {
        std::string str_byte = str_hex.substr(i, 2);

        // Validate hex characters and convert
        char* end_ptr;
        long value = std::strtol(str_byte.c_str(), &end_ptr, 16);

        if (end_ptr != str_byte.c_str() + 2) {
            throw std::invalid_argument("Invalid hex character at position " +
                                        std::to_string(i));
        }

        bytes.push_back(static_cast<uint8_t>(value));
    }

    return bytes;
}
