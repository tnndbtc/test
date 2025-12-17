// ============= serialization.h =============
#ifndef SERIALIZATION_H
#define SERIALIZATION_H

#include <vector>
#include <cstdint>
#include <string>

/**
 * @file serialization.h
 * @brief Binary serialization utilities for transaction data
 *
 * Provides functions for encoding and decoding binary data in a
 * platform-independent format. Uses little-endian byte order for
 * multi-byte integers and variable-length integer (varint) encoding
 * for efficient size representation.
 */

/**
 * @brief Write variable-length integer
 * @param n_value Value to encode (0 to UINT64_MAX)
 * @return Binary data with varint encoding
 *
 * Varint encoding (compatible with protobuf/Bitcoin style):
 * - 0x00-0xFC: 1 byte (value directly)
 * - 0xFD + 2 bytes: 253-65535 (uint16_t LE)
 * - 0xFE + 4 bytes: 65536-4294967295 (uint32_t LE)
 * - 0xFF + 8 bytes: 4294967296+ (uint64_t LE)
 */
std::vector<uint8_t> WriteVarInt(uint64_t n_value);

/**
 * @brief Read variable-length integer
 * @param ptr Pointer to data (will be advanced past varint)
 * @param n_remaining Remaining bytes in buffer (will be decreased)
 * @param n_value Output value
 * @return true on success, false on insufficient data or invalid format
 */
bool ReadVarInt(const uint8_t*& ptr, size_t& n_remaining, uint64_t& n_value);

/**
 * @brief Write uint8_t
 * @param n_value Value to encode
 * @return Binary data (1 byte)
 */
std::vector<uint8_t> WriteUint8(uint8_t n_value);

/**
 * @brief Read uint8_t
 * @param ptr Pointer to data (will be advanced 1 byte)
 * @param n_remaining Remaining bytes in buffer (will be decreased)
 * @param n_value Output value
 * @return true on success, false on insufficient data
 */
bool ReadUint8(const uint8_t*& ptr, size_t& n_remaining, uint8_t& n_value);

/**
 * @brief Write uint64_t in little-endian format
 * @param n_value Value to encode
 * @return Binary data (8 bytes, little-endian)
 */
std::vector<uint8_t> WriteUint64LE(uint64_t n_value);

/**
 * @brief Read uint64_t from little-endian format
 * @param ptr Pointer to data (will be advanced 8 bytes)
 * @param n_remaining Remaining bytes in buffer (will be decreased)
 * @param n_value Output value
 * @return true on success, false on insufficient data
 */
bool ReadUint64LE(const uint8_t*& ptr, size_t& n_remaining, uint64_t& n_value);

/**
 * @brief Write byte array with length prefix
 * @param data Binary data to write
 * @return Varint length + data bytes
 */
std::vector<uint8_t> WriteBytesWithLength(const std::vector<uint8_t>& data);

/**
 * @brief Write byte array without length prefix
 * @param data Binary data to write
 * @param n_length Number of bytes to write
 * @return Binary data
 *
 * If data.size() < n_length, pads with zeros.
 * If data.size() > n_length, truncates.
 */
std::vector<uint8_t> WriteBytes(const std::vector<uint8_t>& data, size_t n_length);

/**
 * @brief Read fixed-length byte array
 * @param ptr Pointer to data (will be advanced n_length bytes)
 * @param n_remaining Remaining bytes in buffer (will be decreased)
 * @param n_length Number of bytes to read
 * @param data Output data
 * @return true on success, false on insufficient data
 */
bool ReadBytes(const uint8_t*& ptr, size_t& n_remaining, size_t n_length,
               std::vector<uint8_t>& data);

/**
 * @brief Read variable-length byte array (length prefix + data)
 * @param ptr Pointer to data (will be advanced)
 * @param n_remaining Remaining bytes in buffer (will be decreased)
 * @param data Output data
 * @return true on success, false on insufficient data or invalid format
 */
bool ReadBytesWithLength(const uint8_t*& ptr, size_t& n_remaining,
                         std::vector<uint8_t>& data);

#endif // SERIALIZATION_H
