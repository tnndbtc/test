#include "scrypt.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <stdexcept>
#include <sstream>

// Derive encryption key from password using scrypt KDF
std::array<uint8_t, CScrypt::DKLEN> CScrypt::DeriveKey(
    const std::string& str_password,
    const std::array<uint8_t, SALT_SIZE>& salt
) {
    std::array<uint8_t, DKLEN> derived_key;

    // Call OpenSSL's EVP_PBE_scrypt
    // Parameters: password, password_len, salt, salt_len, N, r, p, maxmem, out, outlen
    // maxmem calculation: 128 * N * r = 128 * 262144 * 8 = 268435456 (256 MB)
    // Set to 512 MB to be safe
    int result = EVP_PBE_scrypt(
        str_password.c_str(), str_password.size(),
        salt.data(), SALT_SIZE,
        N, R, P,
        536870912,  // maxmem = 512 MB
        derived_key.data(), DKLEN
    );

    if (result != 1) {
        // Get OpenSSL error
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));

        std::ostringstream oss;
        oss << "Scrypt key derivation failed: " << err_buf;
        throw std::runtime_error(oss.str());
    }

    return derived_key;
}

// Generate random salt using OpenSSL RAND_bytes
std::array<uint8_t, CScrypt::SALT_SIZE> CScrypt::GenerateSalt() {
    std::array<uint8_t, SALT_SIZE> salt;

    // Use OpenSSL's cryptographically secure RNG
    int result = RAND_bytes(salt.data(), SALT_SIZE);

    if (result != 1) {
        // Get OpenSSL error
        unsigned long err = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err, err_buf, sizeof(err_buf));

        std::ostringstream oss;
        oss << "Random salt generation failed: " << err_buf;
        throw std::runtime_error(oss.str());
    }

    return salt;
}
