#include "keypair.h"
#include "keccak256.h"
#include <secp256k1.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <mutex>

// Global secp256k1 context (thread-safe initialization)
static secp256k1_context* GetSecp256k1Context() {
    static secp256k1_context* ctx = nullptr;
    static std::once_flag flag;

    std::call_once(flag, []() {
        // Create context for both signing and verification
        ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
        if (!ctx) {
            throw std::runtime_error("Failed to create secp256k1 context");
        }

        // Seed the context's random number generator
        unsigned char seed[32];
        if (RAND_bytes(seed, 32) != 1) {
            secp256k1_context_destroy(ctx);
            throw std::runtime_error("Failed to generate random seed for secp256k1");
        }

        if (secp256k1_context_randomize(ctx, seed) != 1) {
            secp256k1_context_destroy(ctx);
            throw std::runtime_error("Failed to randomize secp256k1 context");
        }

        // Zero the seed after use
        OPENSSL_cleanse(seed, 32);
    });

    return ctx;
}

// Generate new random keypair
CKeyPair::CKeyPair() {
    secp256k1_context* ctx = GetSecp256k1Context();

    // Generate random private key
    do {
        if (RAND_bytes(m_private_key.data(), 32) != 1) {
            throw std::runtime_error("Failed to generate random private key");
        }
    } while (secp256k1_ec_seckey_verify(ctx, m_private_key.data()) != 1);
    // Loop until we get a valid private key (very rare to be invalid)

    // Derive public key and address
    DerivePublicKey();
    DeriveAddress();
}

// Load keypair from existing private key
CKeyPair::CKeyPair(const std::array<uint8_t, 32>& private_key) {
    secp256k1_context* ctx = GetSecp256k1Context();

    // Verify private key is valid
    if (secp256k1_ec_seckey_verify(ctx, private_key.data()) != 1) {
        throw std::invalid_argument("Invalid secp256k1 private key");
    }

    m_private_key = private_key;

    // Derive public key and address
    DerivePublicKey();
    DeriveAddress();
}

// Derive public key from private key using secp256k1
void CKeyPair::DerivePublicKey() {
    secp256k1_context* ctx = GetSecp256k1Context();

    // Create secp256k1 public key structure
    secp256k1_pubkey pubkey;
    if (secp256k1_ec_pubkey_create(ctx, &pubkey, m_private_key.data()) != 1) {
        throw std::runtime_error("Failed to derive public key from private key");
    }

    // Serialize public key in uncompressed format (65 bytes: 0x04 || X || Y)
    unsigned char pubkey_serialized[65];
    size_t pubkey_len = 65;
    if (secp256k1_ec_pubkey_serialize(ctx, pubkey_serialized, &pubkey_len, &pubkey, SECP256K1_EC_UNCOMPRESSED) != 1) {
        throw std::runtime_error("Failed to serialize public key");
    }

    // Check that first byte is 0x04 (uncompressed marker)
    if (pubkey_serialized[0] != 0x04 || pubkey_len != 65) {
        throw std::runtime_error("Unexpected public key format");
    }

    // Copy 64 bytes (X || Y coordinates) without the 0x04 prefix
    std::copy(pubkey_serialized + 1, pubkey_serialized + 65, m_public_key.begin());
}

// Derive Ethereum address from public key using keccak256
void CKeyPair::DeriveAddress() {
    m_str_address = DeriveAddress(m_public_key);
}

// Derive Ethereum address from public key (static method)
std::string CKeyPair::DeriveAddress(const std::array<uint8_t, 64>& public_key) {
    // Compute keccak256 hash of public key (64 bytes)
    std::array<uint8_t, 32> hash = CKeccak256::Hash(public_key.data(), 64);

    // Take last 20 bytes of hash
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 12; i < 32; ++i) {  // Skip first 12 bytes, take last 20
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }

    return oss.str();
}
