// ============= test_hash.cpp =============
#include "unit_test.h"
#include "utils/hash.h"

using namespace UnitTest;

/**
 * @brief Test CHash constructor and data retrieval
 */
TEST(Hash_Constructor) {
    CHash hash1 = CHash::ComputeSHA256("test_data");
    CHash hash2 = CHash::ComputeSHA256("test_data");
    CHash hash3 = CHash::ComputeSHA256("different_data");

    ASSERT_EQUAL(hash1.GetData(), hash2.GetData(), "Same input should produce same hash");
    ASSERT_NOT_EQUAL(hash1.GetData(), hash3.GetData(), "Different input should produce different hash");
}

/**
 * @brief Test CHash empty input
 */
TEST(Hash_EmptyInput) {
    CHash hash = CHash::ComputeSHA256("");

    ASSERT_TRUE(hash.GetData().length() > 0, "Hash of empty string should still produce output");
}

// ============= Binary Constructor Tests =============

/**
 * @brief Test CHash binary constructor with valid 32-byte input
 */
TEST(Hash_BinaryConstructor_Valid) {
    // Create a known 32-byte array
    unsigned char data[32];
    for (int i = 0; i < 32; i++) {
        data[i] = static_cast<unsigned char>(i);
    }

    CHash hash(data, 32);
    const auto& bytes = hash.GetBytes();

    ASSERT_EQUAL(bytes.size(), 32, "Hash should have 32 bytes");
    for (size_t i = 0; i < 32; i++) {
        ASSERT_EQUAL(bytes[i], static_cast<uint8_t>(i), "Byte mismatch at index");
    }
}

/**
 * @brief Test CHash binary constructor with size < 32 (should fill zeros)
 */
TEST(Hash_BinaryConstructor_Short) {
    unsigned char data[16];
    for (int i = 0; i < 16; i++) {
        data[i] = 0xFF;
    }

    CHash hash(data, 16);
    const auto& bytes = hash.GetBytes();

    ASSERT_EQUAL(bytes.size(), 32, "Hash should have 32 bytes");

    // First 16 bytes should be 0xFF
    for (size_t i = 0; i < 16; i++) {
        ASSERT_EQUAL(bytes[i], 0xFF, "First 16 bytes should match input");
    }

    // Remaining bytes should be 0
    for (size_t i = 16; i < 32; i++) {
        ASSERT_EQUAL(bytes[i], 0, "Remaining bytes should be zero-filled");
    }
}

/**
 * @brief Test CHash binary constructor with size = 0 (should fill all zeros)
 */
TEST(Hash_BinaryConstructor_Empty) {
    unsigned char data[1] = {0xFF};  // Data that won't be used

    CHash hash(data, 0);
    const auto& bytes = hash.GetBytes();

    ASSERT_EQUAL(bytes.size(), 32, "Hash should have 32 bytes");
    for (size_t i = 0; i < 32; i++) {
        ASSERT_EQUAL(bytes[i], 0, "All bytes should be zero");
    }
}

/**
 * @brief Test CHash binary constructor differs from string constructor
 */
TEST(Hash_BinaryVsString) {
    // Create hash from string
    CHash hash_string = CHash::ComputeSHA256("test_data");

    // Get the binary representation
    const auto& bytes_string = hash_string.GetBytes();

    // Create hash from those same bytes (not hashing again)
    CHash hash_binary(bytes_string.data(), 32);

    // They should be equal (same binary data)
    ASSERT_EQUAL(hash_string.GetData(), hash_binary.GetData(), "Binary and string constructors should match when using same bytes");
}

// ============= GetBytes() Tests =============

/**
 * @brief Test GetBytes() returns correct size
 */
TEST(Hash_GetBytes_Size) {
    CHash hash = CHash::ComputeSHA256("test_data");
    const auto& bytes = hash.GetBytes();

    ASSERT_EQUAL(bytes.size(), 32, "GetBytes() should return 32 bytes");
}

/**
 * @brief Test GetBytes() roundtrip - binary constructor to GetBytes returns original
 */
TEST(Hash_GetBytes_Roundtrip) {
    unsigned char original_data[32];
    for (int i = 0; i < 32; i++) {
        original_data[i] = static_cast<unsigned char>(i * 7 % 256);  // Some pattern
    }

    CHash hash(original_data, 32);
    const auto& retrieved_bytes = hash.GetBytes();

    ASSERT_EQUAL(retrieved_bytes.size(), 32, "Should have 32 bytes");
    for (size_t i = 0; i < 32; i++) {
        ASSERT_EQUAL(retrieved_bytes[i], original_data[i], "Roundtrip should preserve data");
    }
}

/**
 * @brief Test GetBytes() is const (doesn't modify hash)
 */
TEST(Hash_GetBytes_Const) {
    CHash hash = CHash::ComputeSHA256("test_data");

    const auto& bytes1 = hash.GetBytes();
    const auto& bytes2 = hash.GetBytes();

    // Both should return the same reference
    ASSERT_EQUAL(&bytes1, &bytes2, "GetBytes() should return same reference");

    // Verify data unchanged
    for (size_t i = 0; i < 32; i++) {
        ASSERT_EQUAL(bytes1[i], bytes2[i], "GetBytes() should not modify data");
    }
}

// ============= GetData() Updated Tests =============

/**
 * @brief Test GetData() binary to hex conversion
 */
TEST(Hash_GetData_BinaryToHex) {
    // Create a simple pattern: all zeros except first byte = 0xAB, second = 0xCD
    unsigned char data[32] = {0};
    data[0] = 0xAB;
    data[1] = 0xCD;

    CHash hash(data, 32);
    std::string hex = hash.GetData();

    ASSERT_EQUAL(hex.length(), 64, "Hex string should be 64 characters");

    // First two bytes should be "abcd" (lowercase hex)
    ASSERT_TRUE(hex.substr(0, 4) == "abcd", "Hex conversion should be correct and lowercase");

    // Rest should be zeros "00"
    for (size_t i = 4; i < 64; i++) {
        ASSERT_TRUE(hex[i] == '0', "Remaining hex digits should be 0");
    }
}

// ============= BytesToHex / HexToBytes Utility Tests =============

/**
 * @brief Test BytesToHex and HexToBytes roundtrip conversion
 */
TEST(Hash_BytesToHex_HexToBytes_Roundtrip) {
    // Create a hash with known data
    CHash hash = CHash::ComputeSHA256("test_data");

    // Get hex string representation
    std::string hex_string = hash.GetData();
    ASSERT_EQUAL(hex_string.length(), 64, "Hash hex string should be 64 characters");

    // Convert hex to bytes using HexToBytes
    std::vector<uint8_t> bytes = HexToBytes(hex_string);
    ASSERT_EQUAL(bytes.size(), 32, "HexToBytes should return 32 bytes");

    // Compare with original bytes
    const auto& original_bytes = hash.GetBytes();
    for (size_t i = 0; i < 32; i++) {
        ASSERT_EQUAL(bytes[i], original_bytes[i], "HexToBytes should match original bytes");
    }

    // Convert back to hex using BytesToHex
    std::string hex_roundtrip = BytesToHex(bytes);
    ASSERT_EQUAL(hex_roundtrip, hex_string, "BytesToHex roundtrip should match original hex");
}

/**
 * @brief Test BytesToHex with known data pattern
 */
TEST(Hash_BytesToHex_KnownPattern) {
    // Create a specific byte pattern
    std::vector<uint8_t> bytes = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

    std::string hex = BytesToHex(bytes);

    ASSERT_EQUAL(hex, "0123456789abcdef", "BytesToHex should produce correct hex string");
}

/**
 * @brief Test HexToBytes with known hex string
 */
TEST(Hash_HexToBytes_KnownPattern) {
    std::string hex = "0123456789abcdef";

    std::vector<uint8_t> bytes = HexToBytes(hex);

    ASSERT_EQUAL(bytes.size(), 8, "HexToBytes should return 8 bytes");
    ASSERT_EQUAL(bytes[0], 0x01, "Byte 0 should be 0x01");
    ASSERT_EQUAL(bytes[1], 0x23, "Byte 1 should be 0x23");
    ASSERT_EQUAL(bytes[2], 0x45, "Byte 2 should be 0x45");
    ASSERT_EQUAL(bytes[3], 0x67, "Byte 3 should be 0x67");
    ASSERT_EQUAL(bytes[4], 0x89, "Byte 4 should be 0x89");
    ASSERT_EQUAL(bytes[5], 0xab, "Byte 5 should be 0xab");
    ASSERT_EQUAL(bytes[6], 0xcd, "Byte 6 should be 0xcd");
    ASSERT_EQUAL(bytes[7], 0xef, "Byte 7 should be 0xef");
}

/**
 * @brief Test HexToBytes error handling - odd length
 */
TEST(Hash_HexToBytes_OddLength) {
    try {
        HexToBytes("abc");  // Odd length should throw
        ASSERT_TRUE(false, "HexToBytes should throw for odd length");
    } catch (const std::invalid_argument& e) {
        ASSERT_TRUE(true, "HexToBytes correctly throws for odd length");
    }
}

/**
 * @brief Test HexToBytes error handling - invalid character
 */
TEST(Hash_HexToBytes_InvalidChar) {
    try {
        HexToBytes("01zz");  // 'z' is not valid hex
        ASSERT_TRUE(false, "HexToBytes should throw for invalid hex character");
    } catch (const std::invalid_argument& e) {
        ASSERT_TRUE(true, "HexToBytes correctly throws for invalid character");
    }
}
