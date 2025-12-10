#include "keystore.h"
#include "keccak256.h"
#include "scrypt.h"
#include "aes_ctr.h"
#include "../utils/json.hpp"
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <sys/stat.h>  // For chmod on Unix

using json = nlohmann::json;

// Helper: Convert bytes to hex string
static std::string BytesToHex(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

// Helper: Convert hex string to bytes
static std::vector<uint8_t> HexToBytes(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        throw std::invalid_argument("Hex string must have even length");
    }

    std::vector<uint8_t> bytes;
    bytes.reserve(hex.size() / 2);

    for (size_t i = 0; i < hex.size(); i += 2) {
        std::string byte_str = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
        bytes.push_back(byte);
    }

    return bytes;
}

// Helper: Convert hex string to fixed-size array
template<size_t N>
static std::array<uint8_t, N> HexToArray(const std::string& hex) {
    std::vector<uint8_t> vec = HexToBytes(hex);
    if (vec.size() != N) {
        throw std::invalid_argument("Hex string has wrong length for array");
    }

    std::array<uint8_t, N> arr;
    std::copy(vec.begin(), vec.end(), arr.begin());
    return arr;
}

// Create new keystore from keypair and password
CKeystore::CKeystore(const CKeyPair& keypair, const std::string& str_password) {
    n_version = 3;
    str_cipher = "aes-128-ctr";
    str_kdf = "scrypt";

    // Get address and private key from keypair
    str_address = keypair.GetAddress();
    const std::array<uint8_t, 32>& private_key = keypair.GetPrivateKey();

    // Generate random salt and IV
    salt = CScrypt::GenerateSalt();
    iv = CAesCtr::GenerateIV();

    // Derive key using scrypt
    std::array<uint8_t, 32> derived_key = CScrypt::DeriveKey(str_password, salt);

    // Split derived key into cipher_key and mac_key
    std::array<uint8_t, 16> cipher_key;
    std::array<uint8_t, 16> mac_key;
    std::copy(derived_key.begin(), derived_key.begin() + 16, cipher_key.begin());
    std::copy(derived_key.begin() + 16, derived_key.end(), mac_key.begin());

    // Zero derived_key after splitting
    OPENSSL_cleanse(derived_key.data(), derived_key.size());

    // Encrypt private key using AES-128-CTR
    ciphertext = CAesCtr::Encrypt(private_key.data(), 32, cipher_key, iv);

    // Zero cipher_key after encryption
    OPENSSL_cleanse(cipher_key.data(), cipher_key.size());

    // Compute MAC: keccak256(mac_key + ciphertext)
    mac = ComputeMAC(mac_key, ciphertext);

    // Zero mac_key after MAC computation
    OPENSSL_cleanse(mac_key.data(), mac_key.size());

    // Generate UUID
    str_id = GenerateUUID();
}

// Load keystore from JSON file
CKeystore CKeystore::LoadFromFile(const std::string& str_filepath) {
    std::ifstream file(str_filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open keystore file: " + str_filepath);
    }

    json json_obj;
    try {
        file >> json_obj;
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse keystore JSON: " + std::string(e.what()));
    }

    return FromJson(json_obj);
}

// Save keystore to JSON file
void CKeystore::SaveToFile(const std::string& str_filepath) const {
    json json_obj = ToJson();

    std::ofstream file(str_filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to create keystore file: " + str_filepath);
    }

    file << json_obj.dump(2);  // Pretty print with 2-space indentation
    file.close();

    // Set file permissions to 0600 on Unix-like systems
#ifndef _WIN32
    chmod(str_filepath.c_str(), 0600);
#endif
}

// Decrypt and recover private key
std::array<uint8_t, 32> CKeystore::DecryptPrivateKey(const std::string& str_password) const {
    // Derive key using scrypt
    std::array<uint8_t, 32> derived_key = CScrypt::DeriveKey(str_password, salt);

    // Split derived key into cipher_key and mac_key
    std::array<uint8_t, 16> cipher_key;
    std::array<uint8_t, 16> mac_key;
    std::copy(derived_key.begin(), derived_key.begin() + 16, cipher_key.begin());
    std::copy(derived_key.begin() + 16, derived_key.end(), mac_key.begin());

    // Zero derived_key after splitting
    OPENSSL_cleanse(derived_key.data(), derived_key.size());

    // Verify MAC using constant-time comparison
    std::array<uint8_t, 32> computed_mac = ComputeMAC(mac_key, ciphertext);

    // Zero mac_key after MAC computation
    OPENSSL_cleanse(mac_key.data(), mac_key.size());

    if (CRYPTO_memcmp(computed_mac.data(), mac.data(), 32) != 0) {
        // Zero cipher_key before throwing
        OPENSSL_cleanse(cipher_key.data(), cipher_key.size());
        throw std::runtime_error("Wrong password or corrupted keystore (MAC verification failed)");
    }

    // Decrypt private key
    std::vector<uint8_t> plaintext = CAesCtr::Decrypt(ciphertext.data(), ciphertext.size(), cipher_key, iv);

    // Zero cipher_key after decryption
    OPENSSL_cleanse(cipher_key.data(), cipher_key.size());

    if (plaintext.size() != 32) {
        throw std::runtime_error("Decrypted private key has invalid length");
    }

    std::array<uint8_t, 32> private_key;
    std::copy(plaintext.begin(), plaintext.end(), private_key.begin());

    // Zero plaintext vector
    OPENSSL_cleanse(plaintext.data(), plaintext.size());

    return private_key;
}

// Export to JSON object
nlohmann::json CKeystore::ToJson() const {
    json crypto_obj;
    crypto_obj["ciphertext"] = BytesToHex(ciphertext.data(), ciphertext.size());
    crypto_obj["cipherparams"]["iv"] = BytesToHex(iv.data(), iv.size());
    crypto_obj["cipher"] = str_cipher;
    crypto_obj["kdf"] = str_kdf;
    crypto_obj["kdfparams"]["dklen"] = 32;
    crypto_obj["kdfparams"]["salt"] = BytesToHex(salt.data(), salt.size());
    crypto_obj["kdfparams"]["n"] = 262144;
    crypto_obj["kdfparams"]["r"] = 8;
    crypto_obj["kdfparams"]["p"] = 1;
    crypto_obj["mac"] = BytesToHex(mac.data(), mac.size());

    json json_obj;
    json_obj["version"] = n_version;
    json_obj["id"] = str_id;
    json_obj["address"] = str_address;
    json_obj["crypto"] = crypto_obj;

    return json_obj;
}

// Import from JSON object
CKeystore CKeystore::FromJson(const nlohmann::json& json_obj) {
    CKeystore keystore;

    // Parse version
    if (!json_obj.contains("version") || !json_obj["version"].is_number()) {
        throw std::runtime_error("Missing or invalid 'version' field");
    }
    keystore.n_version = json_obj["version"];
    if (keystore.n_version != 3) {
        throw std::runtime_error("Unsupported keystore version: " + std::to_string(keystore.n_version));
    }

    // Parse ID
    if (!json_obj.contains("id") || !json_obj["id"].is_string()) {
        throw std::runtime_error("Missing or invalid 'id' field");
    }
    keystore.str_id = json_obj["id"];

    // Parse address
    if (!json_obj.contains("address") || !json_obj["address"].is_string()) {
        throw std::runtime_error("Missing or invalid 'address' field");
    }
    keystore.str_address = json_obj["address"];

    // Parse crypto object
    if (!json_obj.contains("crypto") || !json_obj["crypto"].is_object()) {
        throw std::runtime_error("Missing or invalid 'crypto' field");
    }
    const json& crypto_obj = json_obj["crypto"];

    // Parse ciphertext
    if (!crypto_obj.contains("ciphertext") || !crypto_obj["ciphertext"].is_string()) {
        throw std::runtime_error("Missing or invalid 'ciphertext' field");
    }
    keystore.ciphertext = HexToBytes(crypto_obj["ciphertext"]);

    // Parse IV
    if (!crypto_obj.contains("cipherparams") || !crypto_obj["cipherparams"].is_object()) {
        throw std::runtime_error("Missing or invalid 'cipherparams' field");
    }
    if (!crypto_obj["cipherparams"].contains("iv") || !crypto_obj["cipherparams"]["iv"].is_string()) {
        throw std::runtime_error("Missing or invalid 'iv' field");
    }
    keystore.iv = HexToArray<16>(crypto_obj["cipherparams"]["iv"]);

    // Parse cipher
    if (!crypto_obj.contains("cipher") || !crypto_obj["cipher"].is_string()) {
        throw std::runtime_error("Missing or invalid 'cipher' field");
    }
    keystore.str_cipher = crypto_obj["cipher"];

    // Parse KDF
    if (!crypto_obj.contains("kdf") || !crypto_obj["kdf"].is_string()) {
        throw std::runtime_error("Missing or invalid 'kdf' field");
    }
    keystore.str_kdf = crypto_obj["kdf"];

    // Parse salt
    if (!crypto_obj.contains("kdfparams") || !crypto_obj["kdfparams"].is_object()) {
        throw std::runtime_error("Missing or invalid 'kdfparams' field");
    }
    if (!crypto_obj["kdfparams"].contains("salt") || !crypto_obj["kdfparams"]["salt"].is_string()) {
        throw std::runtime_error("Missing or invalid 'salt' field");
    }
    keystore.salt = HexToArray<32>(crypto_obj["kdfparams"]["salt"]);

    // Parse MAC
    if (!crypto_obj.contains("mac") || !crypto_obj["mac"].is_string()) {
        throw std::runtime_error("Missing or invalid 'mac' field");
    }
    keystore.mac = HexToArray<32>(crypto_obj["mac"]);

    return keystore;
}

// Generate UUID v4
std::string CKeystore::GenerateUUID() {
    unsigned char bytes[16];
    if (RAND_bytes(bytes, 16) != 1) {
        throw std::runtime_error("Failed to generate random UUID");
    }

    // Set version to 4 (random UUID)
    bytes[6] = (bytes[6] & 0x0f) | 0x40;

    // Set variant to RFC 4122
    bytes[8] = (bytes[8] & 0x3f) | 0x80;

    // Format as UUID string: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            oss << '-';
        }
        oss << std::setw(2) << static_cast<int>(bytes[i]);
    }

    return oss.str();
}

// Verify MAC
bool CKeystore::VerifyMAC(const std::string& str_password) const {
    // Derive key using scrypt
    std::array<uint8_t, 32> derived_key = CScrypt::DeriveKey(str_password, salt);

    // Extract mac_key (last 16 bytes)
    std::array<uint8_t, 16> mac_key;
    std::copy(derived_key.begin() + 16, derived_key.end(), mac_key.begin());

    // Zero derived_key
    OPENSSL_cleanse(derived_key.data(), derived_key.size());

    // Compute MAC
    std::array<uint8_t, 32> computed_mac = ComputeMAC(mac_key, ciphertext);

    // Zero mac_key
    OPENSSL_cleanse(mac_key.data(), mac_key.size());

    // Use constant-time comparison
    return CRYPTO_memcmp(computed_mac.data(), mac.data(), 32) == 0;
}

// Compute MAC: keccak256(mac_key + ciphertext)
std::array<uint8_t, 32> CKeystore::ComputeMAC(
    const std::array<uint8_t, 16>& mac_key,
    const std::vector<uint8_t>& ciphertext
) {
    // Concatenate mac_key and ciphertext
    std::vector<uint8_t> mac_input;
    mac_input.reserve(mac_key.size() + ciphertext.size());
    mac_input.insert(mac_input.end(), mac_key.begin(), mac_key.end());
    mac_input.insert(mac_input.end(), ciphertext.begin(), ciphertext.end());

    // Compute keccak256 hash
    return CKeccak256::Hash(mac_input);
}
