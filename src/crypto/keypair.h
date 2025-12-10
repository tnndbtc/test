#ifndef KEYPAIR_H
#define KEYPAIR_H

#include <array>
#include <cstdint>
#include <string>

/**
 * CKeyPair - secp256k1 ECC key pair generation and Ethereum address derivation
 *
 * Generates secp256k1 private/public keys and derives Ethereum-compatible addresses.
 *
 * Address derivation (Ethereum style):
 *   1. Generate 32-byte private key using secure random
 *   2. Derive 64-byte uncompressed public key (X||Y coordinates, no 0x04 prefix)
 *   3. Compute keccak256(public_key_64_bytes) → 32 bytes
 *   4. Take last 20 bytes → address
 *   5. Format as 40-character lowercase hex string (no 0x prefix)
 *
 * Usage:
 *   CKeyPair keypair;  // Generate new random keypair
 *   std::string address = keypair.GetAddress();
 *   const std::array<uint8_t, 32>& private_key = keypair.GetPrivateKey();
 */
class CKeyPair {
private:
    std::array<uint8_t, 32> m_private_key;  // 32-byte private key
    std::array<uint8_t, 64> m_public_key;   // 64-byte uncompressed public key (no 0x04 prefix)
    std::string m_str_address;              // 40-char hex address (20 bytes)

public:
    /**
     * Generate new random keypair
     * @throws std::runtime_error if key generation fails
     */
    CKeyPair();

    /**
     * Load keypair from existing private key
     * @param private_key - 32-byte private key
     * @throws std::runtime_error if private key is invalid or public key derivation fails
     */
    explicit CKeyPair(const std::array<uint8_t, 32>& private_key);

    /**
     * Get private key
     * WARNING: Handle with care! Private keys must be kept secret.
     */
    const std::array<uint8_t, 32>& GetPrivateKey() const { return m_private_key; }

    /**
     * Get public key (64 bytes uncompressed, no 0x04 prefix)
     */
    const std::array<uint8_t, 64>& GetPublicKey() const { return m_public_key; }

    /**
     * Get Ethereum address (40-char lowercase hex, no 0x prefix)
     */
    std::string GetAddress() const { return m_str_address; }

    /**
     * Derive Ethereum address from public key
     * @param public_key - 64-byte uncompressed public key (no 0x04 prefix)
     * @return 40-character lowercase hex address (no 0x prefix)
     */
    static std::string DeriveAddress(const std::array<uint8_t, 64>& public_key);

private:
    /**
     * Derive public key from private key using secp256k1
     * @throws std::runtime_error if derivation fails
     */
    void DerivePublicKey();

    /**
     * Derive Ethereum address from public key using keccak256
     */
    void DeriveAddress();
};

#endif // KEYPAIR_H
