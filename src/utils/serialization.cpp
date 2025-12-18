// ============= serialization.cpp =============
/**
 * @file serialization.cpp
 * @brief Implementation of binary serialization utilities
 */

#include "serialization.h"
#include <cstring>
#include <algorithm>

// Platform-specific byte order conversion
#ifdef _WIN32
    #define NOMINMAX  // Prevent Windows min/max macros from conflicting with std::min/std::max
    #include <winsock2.h>
    #define htole16(x) (x)
    #define htole32(x) (x)
    #define htole64(x) (x)
    #define le16toh(x) (x)
    #define le32toh(x) (x)
    #define le64toh(x) (x)
#elif defined(__APPLE__)
    #include <libkern/OSByteOrder.h>
    #define htole16(x) OSSwapHostToLittleInt16(x)
    #define htole32(x) OSSwapHostToLittleInt32(x)
    #define htole64(x) OSSwapHostToLittleInt64(x)
    #define le16toh(x) OSSwapLittleToHostInt16(x)
    #define le32toh(x) OSSwapLittleToHostInt32(x)
    #define le64toh(x) OSSwapLittleToHostInt64(x)
#else
    #include <endian.h>
#endif

std::vector<uint8_t> WriteVarInt(uint64_t n_value) {
    std::vector<uint8_t> result;

    if (n_value < 0xFD) {
        // 1 byte
        result.push_back(static_cast<uint8_t>(n_value));
    } else if (n_value <= 0xFFFF) {
        // 3 bytes: 0xFD + uint16_t LE
        result.push_back(0xFD);
        uint16_t n_val_le = htole16(static_cast<uint16_t>(n_value));
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&n_val_le);
        result.insert(result.end(), p, p + 2);
    } else if (n_value <= 0xFFFFFFFF) {
        // 5 bytes: 0xFE + uint32_t LE
        result.push_back(0xFE);
        uint32_t n_val_le = htole32(static_cast<uint32_t>(n_value));
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&n_val_le);
        result.insert(result.end(), p, p + 4);
    } else {
        // 9 bytes: 0xFF + uint64_t LE
        result.push_back(0xFF);
        uint64_t n_val_le = htole64(n_value);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&n_val_le);
        result.insert(result.end(), p, p + 8);
    }

    return result;
}

bool ReadVarInt(const uint8_t*& ptr, size_t& n_remaining, uint64_t& n_value) {
    if (n_remaining < 1) {
        return false;
    }

    uint8_t n_first = *ptr;
    ptr++;
    n_remaining--;

    if (n_first < 0xFD) {
        n_value = n_first;
        return true;
    } else if (n_first == 0xFD) {
        if (n_remaining < 2) return false;
        uint16_t n_val_le;
        std::memcpy(&n_val_le, ptr, 2);
        n_value = le16toh(n_val_le);
        ptr += 2;
        n_remaining -= 2;
        return true;
    } else if (n_first == 0xFE) {
        if (n_remaining < 4) return false;
        uint32_t n_val_le;
        std::memcpy(&n_val_le, ptr, 4);
        n_value = le32toh(n_val_le);
        ptr += 4;
        n_remaining -= 4;
        return true;
    } else {  // 0xFF
        if (n_remaining < 8) return false;
        uint64_t n_val_le;
        std::memcpy(&n_val_le, ptr, 8);
        n_value = le64toh(n_val_le);
        ptr += 8;
        n_remaining -= 8;
        return true;
    }
}

std::vector<uint8_t> WriteUint8(uint8_t n_value) {
    return {n_value};
}

bool ReadUint8(const uint8_t*& ptr, size_t& n_remaining, uint8_t& n_value) {
    if (n_remaining < 1) {
        return false;
    }
    n_value = *ptr;
    ptr++;
    n_remaining--;
    return true;
}

std::vector<uint8_t> WriteUint64LE(uint64_t n_value) {
    uint64_t n_val_le = htole64(n_value);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&n_val_le);
    return std::vector<uint8_t>(p, p + 8);
}

bool ReadUint64LE(const uint8_t*& ptr, size_t& n_remaining, uint64_t& n_value) {
    if (n_remaining < 8) {
        return false;
    }
    uint64_t n_val_le;
    std::memcpy(&n_val_le, ptr, 8);
    n_value = le64toh(n_val_le);
    ptr += 8;
    n_remaining -= 8;
    return true;
}

std::vector<uint8_t> WriteBytesWithLength(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> result = WriteVarInt(data.size());
    result.insert(result.end(), data.begin(), data.end());
    return result;
}

std::vector<uint8_t> WriteBytes(const std::vector<uint8_t>& data, size_t n_length) {
    std::vector<uint8_t> result(n_length, 0);
    size_t n_copy_len = std::min(data.size(), n_length);
    std::copy(data.begin(), data.begin() + n_copy_len, result.begin());
    return result;
}

bool ReadBytes(const uint8_t*& ptr, size_t& n_remaining, size_t n_length,
               std::vector<uint8_t>& data) {
    if (n_remaining < n_length) {
        return false;
    }
    data.assign(ptr, ptr + n_length);
    ptr += n_length;
    n_remaining -= n_length;
    return true;
}

bool ReadBytesWithLength(const uint8_t*& ptr, size_t& n_remaining,
                         std::vector<uint8_t>& data) {
    uint64_t n_length;
    if (!ReadVarInt(ptr, n_remaining, n_length)) {
        return false;
    }
    return ReadBytes(ptr, n_remaining, static_cast<size_t>(n_length), data);
}
