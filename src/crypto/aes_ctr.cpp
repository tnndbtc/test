#include "aes_ctr.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <stdexcept>
#include <sstream>
#include <memory>

// Encrypt data using AES-128-CTR
std::vector<uint8_t> CAesCtr::Encrypt(
    const uint8_t* data, size_t n_len,
    const std::array<uint8_t, KEY_SIZE>& key,
    const std::array<uint8_t, IV_SIZE>& iv
) {
    // Create and initialize context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create AES-CTR context");
    }

    // Use unique_ptr for automatic cleanup
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx_ptr(ctx, EVP_CIPHER_CTX_free);

    // Initialize encryption operation with AES-128-CTR
    if (EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, key.data(), iv.data()) != 1) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));

        std::ostringstream oss;
        oss << "AES-128-CTR encrypt init failed: " << err_buf;
        throw std::runtime_error(oss.str());
    }

    // Allocate output buffer (same size as input for CTR mode)
    std::vector<uint8_t> ciphertext(n_len);
    int len = 0;

    // Encrypt data
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, data, static_cast<int>(n_len)) != 1) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));

        std::ostringstream oss;
        oss << "AES-128-CTR encrypt update failed: " << err_buf;
        throw std::runtime_error(oss.str());
    }

    int ciphertext_len = len;

    // Finalize encryption (no padding in CTR mode, but still need to call)
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));

        std::ostringstream oss;
        oss << "AES-128-CTR encrypt final failed: " << err_buf;
        throw std::runtime_error(oss.str());
    }

    ciphertext_len += len;
    ciphertext.resize(ciphertext_len);

    return ciphertext;
}

// Decrypt data using AES-128-CTR
std::vector<uint8_t> CAesCtr::Decrypt(
    const uint8_t* data, size_t n_len,
    const std::array<uint8_t, KEY_SIZE>& key,
    const std::array<uint8_t, IV_SIZE>& iv
) {
    // Create and initialize context
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create AES-CTR context");
    }

    // Use unique_ptr for automatic cleanup
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx_ptr(ctx, EVP_CIPHER_CTX_free);

    // Initialize decryption operation with AES-128-CTR
    if (EVP_DecryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, key.data(), iv.data()) != 1) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));

        std::ostringstream oss;
        oss << "AES-128-CTR decrypt init failed: " << err_buf;
        throw std::runtime_error(oss.str());
    }

    // Allocate output buffer (same size as input for CTR mode)
    std::vector<uint8_t> plaintext(n_len);
    int len = 0;

    // Decrypt data
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, data, static_cast<int>(n_len)) != 1) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));

        std::ostringstream oss;
        oss << "AES-128-CTR decrypt update failed: " << err_buf;
        throw std::runtime_error(oss.str());
    }

    int plaintext_len = len;

    // Finalize decryption (no padding in CTR mode, but still need to call)
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));

        std::ostringstream oss;
        oss << "AES-128-CTR decrypt final failed: " << err_buf;
        throw std::runtime_error(oss.str());
    }

    plaintext_len += len;
    plaintext.resize(plaintext_len);

    return plaintext;
}

// Generate random initialization vector (IV)
std::array<uint8_t, CAesCtr::IV_SIZE> CAesCtr::GenerateIV() {
    std::array<uint8_t, IV_SIZE> iv;

    // Use OpenSSL's cryptographically secure RNG
    int result = RAND_bytes(iv.data(), IV_SIZE);

    if (result != 1) {
        // Get OpenSSL error
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));

        std::ostringstream oss;
        oss << "Random IV generation failed: " << err_buf;
        throw std::runtime_error(oss.str());
    }

    return iv;
}
