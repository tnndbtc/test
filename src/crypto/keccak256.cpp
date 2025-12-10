#include "keccak256.h"
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <cctype>

// Include tiny-keccak library
extern "C" {
#include "../utils/tiny-keccak.h"
}

// Compute Keccak-256 hash from raw data
std::array<uint8_t, CKeccak256::HASH_SIZE> CKeccak256::Hash(const uint8_t* data, size_t n_len) {
    std::array<uint8_t, HASH_SIZE> hash;

    // Call sha3_256 from tiny-keccak library
    // Note: tiny-keccak's sha3_256 is actually Keccak-256 (pre-NIST standard)
    int result = sha3_256(hash.data(), HASH_SIZE, data, n_len);

    if (result != 0) {
        throw std::runtime_error("Keccak-256 hash computation failed");
    }

    return hash;
}

// Compute Keccak-256 hash from vector
std::array<uint8_t, CKeccak256::HASH_SIZE> CKeccak256::Hash(const std::vector<uint8_t>& data) {
    return Hash(data.data(), data.size());
}

// Compute Keccak-256 hash from string
std::array<uint8_t, CKeccak256::HASH_SIZE> CKeccak256::Hash(const std::string& str) {
    return Hash(reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

// Convert hash to lowercase hex string (no 0x prefix)
std::string CKeccak256::ToHex(const std::array<uint8_t, HASH_SIZE>& hash) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');

    for (uint8_t byte : hash) {
        oss << std::setw(2) << static_cast<int>(byte);
    }

    return oss.str();
}

// Convert hex string to hash array
std::array<uint8_t, CKeccak256::HASH_SIZE> CKeccak256::FromHex(const std::string& str_hex) {
    // Remove 0x prefix if present
    std::string hex = str_hex;
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex = hex.substr(2);
    }

    // Check length (should be 64 hex chars for 32 bytes)
    if (hex.size() != HASH_SIZE * 2) {
        throw std::invalid_argument("Invalid hex string length for Keccak-256 hash");
    }

    std::array<uint8_t, HASH_SIZE> hash;

    for (size_t i = 0; i < HASH_SIZE; ++i) {
        hash[i] = HexToByte(hex.substr(i * 2, 2));
    }

    return hash;
}

// Helper: Convert byte to 2-char hex string
std::string CKeccak256::ByteToHex(uint8_t byte) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
    return oss.str();
}

// Helper: Convert 2-char hex string to byte
uint8_t CKeccak256::HexToByte(const std::string& str_hex) {
    if (str_hex.size() != 2) {
        throw std::invalid_argument("Invalid hex string: must be 2 characters");
    }

    int value;
    std::istringstream iss(str_hex);
    iss >> std::hex >> value;

    if (iss.fail()) {
        throw std::invalid_argument("Invalid hex character in string");
    }

    return static_cast<uint8_t>(value);
}
