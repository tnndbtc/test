#include "unit_test.h"
#include "../crypto/keccak256.h"
#include "../crypto/scrypt.h"
#include "../crypto/aes_ctr.h"
#include "../crypto/keypair.h"
#include "../crypto/keystore.h"
#include <openssl/crypto.h>

using namespace UnitTest;

// Test CKeccak256
// NOTE: Uses original Keccak-256 (Ethereum standard with 0x01 padding)
// NOT NIST SHA3-256 (which uses 0x06 padding)
TEST(test_keccak256_empty) {
    // Test empty string (known vector for Keccak-256)
    auto hash = CKeccak256::Hash("");
    std::string hex = CKeccak256::ToHex(hash);

    // Keccak-256("") = c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470
    ASSERT_EQUAL(hex, std::string("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"), "Empty string hash");
}

TEST(test_keccak256_abc) {
    // Test "abc" (known vector for Keccak-256)
    auto hash = CKeccak256::Hash("abc");
    std::string hex = CKeccak256::ToHex(hash);

    // Keccak-256("abc") = 4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45
    ASSERT_EQUAL(hex, std::string("4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45"), "ABC hash");
}

TEST(test_keccak256_vector) {
    // Test with vector input
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    auto hash = CKeccak256::Hash(data);
    ASSERT_EQUAL(hash.size(), size_t(32), "Hash should be 32 bytes");
}

TEST(test_keccak256_hex_conversion) {
    // Test hex conversion
    auto hash = CKeccak256::Hash("test");
    std::string hex = CKeccak256::ToHex(hash);
    ASSERT_EQUAL(hex.size(), size_t(64), "Hex should be 64 chars");  // 32 bytes = 64 hex chars

    // Test FromHex
    auto hash2 = CKeccak256::FromHex(hex);
    ASSERT_TRUE(hash == hash2, "FromHex should match original hash");
}

// Test CScrypt
TEST(test_scrypt_generate_salt) {
    auto salt1 = CScrypt::GenerateSalt();
    auto salt2 = CScrypt::GenerateSalt();

    ASSERT_EQUAL(salt1.size(), size_t(32), "Salt should be 32 bytes");
    ASSERT_EQUAL(salt2.size(), size_t(32), "Salt should be 32 bytes");
    ASSERT_TRUE(salt1 != salt2, "Salts should be different");
}

TEST(test_scrypt_derive_key) {
    std::array<uint8_t, 32> salt;
    for (size_t i = 0; i < 32; ++i) salt[i] = static_cast<uint8_t>(i);

    auto key1 = CScrypt::DeriveKey("password123", salt);
    auto key2 = CScrypt::DeriveKey("password123", salt);

    ASSERT_EQUAL(key1.size(), size_t(32), "Derived key should be 32 bytes");
    ASSERT_TRUE(key1 == key2, "Same password and salt should give same key");
}

TEST(test_scrypt_different_passwords) {
    std::array<uint8_t, 32> salt;
    for (size_t i = 0; i < 32; ++i) salt[i] = static_cast<uint8_t>(i);

    auto key1 = CScrypt::DeriveKey("password1", salt);
    auto key2 = CScrypt::DeriveKey("password2", salt);

    ASSERT_TRUE(key1 != key2, "Different passwords should give different keys");
}

// Test CAesCtr
TEST(test_aes_ctr_generate_iv) {
    auto iv1 = CAesCtr::GenerateIV();
    auto iv2 = CAesCtr::GenerateIV();

    ASSERT_EQUAL(iv1.size(), size_t(16), "IV should be 16 bytes");
    ASSERT_EQUAL(iv2.size(), size_t(16), "IV should be 16 bytes");
    ASSERT_TRUE(iv1 != iv2, "IVs should be different");
}

TEST(test_aes_ctr_encrypt_decrypt) {
    std::array<uint8_t, 16> key;
    std::array<uint8_t, 16> iv;
    for (size_t i = 0; i < 16; ++i) {
        key[i] = static_cast<uint8_t>(i);
        iv[i] = static_cast<uint8_t>(i * 2);
    }

    std::vector<uint8_t> plaintext = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto ciphertext = CAesCtr::Encrypt(plaintext.data(), plaintext.size(), key, iv);
    auto decrypted = CAesCtr::Decrypt(ciphertext.data(), ciphertext.size(), key, iv);

    ASSERT_TRUE(decrypted == plaintext, "Decrypt should match original plaintext");
}

TEST(test_aes_ctr_different_keys) {
    std::array<uint8_t, 16> key1, key2, iv;
    for (size_t i = 0; i < 16; ++i) {
        key1[i] = static_cast<uint8_t>(i);
        key2[i] = static_cast<uint8_t>(i + 1);
        iv[i] = static_cast<uint8_t>(i);
    }

    std::vector<uint8_t> plaintext = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto ciphertext1 = CAesCtr::Encrypt(plaintext.data(), plaintext.size(), key1, iv);
    auto ciphertext2 = CAesCtr::Encrypt(plaintext.data(), plaintext.size(), key2, iv);

    ASSERT_TRUE(ciphertext1 != ciphertext2, "Different keys should give different ciphertext");
}

// Test CKeyPair
TEST(test_keypair_generation) {
    CKeyPair keypair;

    ASSERT_EQUAL(keypair.GetPrivateKey().size(), size_t(32), "Private key should be 32 bytes");
    ASSERT_EQUAL(keypair.GetPublicKey().size(), size_t(64), "Public key should be 64 bytes");
    ASSERT_EQUAL(keypair.GetAddress().size(), size_t(40), "Address should be 40 hex chars");
}

TEST(test_keypair_uniqueness) {
    CKeyPair keypair1;
    CKeyPair keypair2;

    ASSERT_TRUE(keypair1.GetPrivateKey() != keypair2.GetPrivateKey(), "Private keys should be unique");
    ASSERT_TRUE(keypair1.GetPublicKey() != keypair2.GetPublicKey(), "Public keys should be unique");
    ASSERT_NOT_EQUAL(keypair1.GetAddress(), keypair2.GetAddress(), "Addresses should be unique");
}

TEST(test_keypair_from_private_key) {
    CKeyPair keypair1;
    auto private_key = keypair1.GetPrivateKey();

    CKeyPair keypair2(private_key);

    ASSERT_TRUE(keypair1.GetPrivateKey() == keypair2.GetPrivateKey(), "Private keys should match");
    ASSERT_TRUE(keypair1.GetPublicKey() == keypair2.GetPublicKey(), "Public keys should match");
    ASSERT_EQUAL(keypair1.GetAddress(), keypair2.GetAddress(), "Addresses should match");
}

TEST(test_keypair_address_format) {
    CKeyPair keypair;
    std::string address = keypair.GetAddress();

    // Address should be 40 hex characters (lowercase)
    ASSERT_EQUAL(address.size(), size_t(40), "Address should be 40 chars");
    for (char c : address) {
        bool is_hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        ASSERT_TRUE(is_hex, "Address should contain only lowercase hex");
    }
}

// Test CKeystore
TEST(test_keystore_create_and_decrypt) {
    CKeyPair keypair;
    std::string password = "testpassword123";

    CKeystore keystore(keypair, password);

    // Decrypt and verify
    auto decrypted_key = keystore.DecryptPrivateKey(password);
    ASSERT_TRUE(decrypted_key == keypair.GetPrivateKey(), "Decrypted key should match original");
}

TEST(test_keystore_wrong_password) {
    CKeyPair keypair;
    std::string password = "correct_password";

    CKeystore keystore(keypair, password);

    // Try to decrypt with wrong password
    bool threw_exception = false;
    try {
        keystore.DecryptPrivateKey("wrong_password");
    } catch (const std::exception& e) {
        threw_exception = true;
        std::string error_msg = e.what();
        bool has_mac_error = error_msg.find("MAC") != std::string::npos ||
                            error_msg.find("Wrong password") != std::string::npos;
        ASSERT_TRUE(has_mac_error, "Should throw MAC verification error");
    }
    ASSERT_TRUE(threw_exception, "Should throw exception for wrong password");
}

TEST(test_keystore_address_matches) {
    CKeyPair keypair;
    std::string password = "testpassword";

    CKeystore keystore(keypair, password);

    ASSERT_EQUAL(keystore.GetAddress(), keypair.GetAddress(), "Keystore address should match keypair address");
}

TEST(test_keystore_json_format) {
    CKeyPair keypair;
    std::string password = "testpassword";

    CKeystore keystore(keypair, password);
    auto json = keystore.ToJson();

    // Check required fields
    ASSERT_TRUE(json.contains("version"), "JSON should contain version");
    ASSERT_TRUE(json.contains("id"), "JSON should contain id");
    ASSERT_TRUE(json.contains("address"), "JSON should contain address");
    ASSERT_TRUE(json.contains("crypto"), "JSON should contain crypto");

    ASSERT_EQUAL(json["version"], 3, "Version should be 3");
    ASSERT_TRUE(json["crypto"].contains("cipher"), "Crypto should contain cipher");
    ASSERT_TRUE(json["crypto"].contains("ciphertext"), "Crypto should contain ciphertext");
    ASSERT_TRUE(json["crypto"].contains("kdf"), "Crypto should contain kdf");
    ASSERT_TRUE(json["crypto"].contains("mac"), "Crypto should contain mac");
}

TEST(test_keystore_save_and_load) {
    CKeyPair keypair;
    std::string password = "testpassword";
    std::string filepath = "test_keystore_temp.json";

    // Create and save
    CKeystore keystore1(keypair, password);
    keystore1.SaveToFile(filepath);

    // Load and verify
    CKeystore keystore2 = CKeystore::LoadFromFile(filepath);
    ASSERT_EQUAL(keystore2.GetAddress(), keypair.GetAddress(), "Loaded address should match");

    auto decrypted = keystore2.DecryptPrivateKey(password);
    ASSERT_TRUE(decrypted == keypair.GetPrivateKey(), "Decrypted key should match original");

    // Cleanup
    std::remove(filepath.c_str());
}

TEST(test_keystore_roundtrip) {
    CKeyPair keypair;
    std::string password = "mypassword123";

    // Create keystore
    CKeystore keystore1(keypair, password);
    auto json = keystore1.ToJson();

    // Parse back from JSON
    CKeystore keystore2 = CKeystore::FromJson(json);

    // Verify address matches
    ASSERT_EQUAL(keystore2.GetAddress(), keypair.GetAddress(), "Roundtrip address should match");

    // Decrypt and verify private key
    auto decrypted = keystore2.DecryptPrivateKey(password);
    ASSERT_TRUE(decrypted == keypair.GetPrivateKey(), "Roundtrip private key should match");
}

TEST(test_keystore_memory_cleanup) {
    CKeyPair keypair;
    std::string password = "testpassword";

    CKeystore keystore(keypair, password);

    // After decryption, sensitive data should be zeroed
    auto decrypted1 = keystore.DecryptPrivateKey(password);
    auto decrypted2 = keystore.DecryptPrivateKey(password);

    // Both decryptions should give same result
    ASSERT_TRUE(decrypted1 == decrypted2, "Multiple decryptions should match");
    ASSERT_TRUE(decrypted1 == keypair.GetPrivateKey(), "Decrypted should match original");
}
