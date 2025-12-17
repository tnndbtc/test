// ============= address.h =============
#ifndef ADDRESS_H
#define ADDRESS_H

#include <vector>
#include <string>
#include <cstdint>

/**
 * @file address.h
 * @brief Address utility functions for blockweave addresses
 *
 * Provides functions for address validation, conversion between binary
 * and hexadecimal representations, and address generation.
 */

// Address length constant (20 bytes, similar to Ethereum)
constexpr size_t ADDRESS_LENGTH = 20;

/**
 * @brief Validate address format and length
 * @param address Binary address to validate
 * @return true if address is valid (20 bytes), false otherwise
 */
bool IsValidAddress(const std::vector<uint8_t>& address);

/**
 * @brief Convert binary address to hexadecimal string
 * @param address Binary address (20 bytes)
 * @return Hexadecimal string representation with "0x" prefix
 *
 * Example: [0xAB, 0xCD, ...] -> "0xABCD..."
 */
std::string AddressToHex(const std::vector<uint8_t>& address);

/**
 * @brief Convert hexadecimal string to binary address
 * @param str_hex Hexadecimal string (with or without "0x" prefix)
 * @return Binary address vector (20 bytes), or empty vector on error
 *
 * Example: "0xABCD..." -> [0xAB, 0xCD, ...]
 * Note: Uses HexToBytes from hash.h
 */
std::vector<uint8_t> HexToAddress(const std::string& str_hex);

#endif // ADDRESS_H
