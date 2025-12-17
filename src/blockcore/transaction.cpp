// ============= transaction.cpp =============
/**
 * @file transaction.cpp
 * @brief Implementation of blockweave transaction
 *
 * Provides transaction construction with automatic ID generation
 * and timestamping for data storage operations.
 */

#include "transaction.h"
#include "utils/settings.h"
#include "utils/hash.h"
#include <chrono>
#include <cstring>
#include <sstream>

// Platform-specific byte order conversion
#ifdef __APPLE__
    #include <machine/endian.h>
    #include <libkern/OSByteOrder.h>
    #define htobe64(x) OSSwapHostToBigInt64(x)
    #define be64toh(x) OSSwapBigToHostInt64(x)
#elif defined(__linux__)
    #include <endian.h>
    #include <arpa/inet.h>
#elif defined(_WIN32)
    #include <winsock2.h>
    #define htobe64(x) htonll(x)
    #define be64toh(x) ntohll(x)
#endif

namespace {
    /**
     * @brief Escape special characters for JSON string values
     * @param str String to escape
     * @return Escaped string safe for JSON
     *
     * Escapes quotes, backslashes, and control characters to ensure
     * valid JSON output.
     */
    std::string EscapeJsonString(const std::string& str) {
        std::ostringstream oss;
        for (char c : str) {
            switch (c) {
                case '\"': oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;
                default: oss << c; break;
            }
        }
        return oss.str();
    }

    /**
     * @brief Convert TransactionType enum to string
     * @param type Transaction type enum value
     * @return String representation of transaction type
     */
    std::string TransactionTypeToString(TransactionType type) {
        switch (type) {
            case TransactionType::TRANSFER: return "TRANSFER";
            default: return "UNKNOWN";
        }
    }
}

std::string CTransaction::Serialize() const {
    // Calculate total size
    size_t total_size = BLOCK_UINT8_SIZE +              // version
                        BLOCK_UINT64_SIZE +             // nonce
                        20 +                            // from address (20 bytes)
                        BLOCK_UINT8_SIZE +              // tx_type
                        BLOCK_UINT32_SIZE + m_data.size() +  // data_length + data
                        BLOCK_UINT64_SIZE +             // fee
                        BLOCK_UINT32_SIZE + m_signature.size() +  // signature_length + signature
                        BLOCK_UINT32_SIZE + m_str_metadata.length();  // metadata_length + metadata

    std::string str_result;
    str_result.resize(total_size);
    size_t offset = 0;

    // Write version (1 byte)
    str_result[offset] = static_cast<uint8_t>(m_n_version);
    offset += BLOCK_UINT8_SIZE;

    // Write nonce (8 bytes, network byte order)
    uint64_t n_nonce_network = htobe64(m_n_nonce);
    std::memcpy(&str_result[offset], &n_nonce_network, BLOCK_UINT64_SIZE);
    offset += BLOCK_UINT64_SIZE;

    // Write from address (20 bytes)
    if (m_from.size() >= 20) {
        std::memcpy(&str_result[offset], m_from.data(), 20);
    } else {
        // Pad with zeros if from address is smaller than 20 bytes
        std::memcpy(&str_result[offset], m_from.data(), m_from.size());
        std::memset(&str_result[offset + m_from.size()], 0, 20 - m_from.size());
    }
    offset += 20;

    // Write tx_type (1 byte)
    str_result[offset] = static_cast<uint8_t>(m_type);
    offset += BLOCK_UINT8_SIZE;

    // Write data (length + data)
    uint32_t n_data_len = htonl(static_cast<uint32_t>(m_data.size()));
    std::memcpy(&str_result[offset], &n_data_len, BLOCK_UINT32_SIZE);
    offset += BLOCK_UINT32_SIZE;
    std::memcpy(&str_result[offset], m_data.data(), m_data.size());
    offset += m_data.size();

    // Write fee (8 bytes, network byte order)
    uint64_t n_fee_network = htobe64(m_n_fee);
    std::memcpy(&str_result[offset], &n_fee_network, BLOCK_UINT64_SIZE);
    offset += BLOCK_UINT64_SIZE;

    // Write signature (length + data)
    uint32_t n_signature_len = htonl(static_cast<uint32_t>(m_signature.size()));
    std::memcpy(&str_result[offset], &n_signature_len, BLOCK_UINT32_SIZE);
    offset += BLOCK_UINT32_SIZE;
    std::memcpy(&str_result[offset], m_signature.data(), m_signature.size());
    offset += m_signature.size();

    // Write metadata (length + data)
    uint32_t n_metadata_len = htonl(static_cast<uint32_t>(m_str_metadata.length()));
    std::memcpy(&str_result[offset], &n_metadata_len, BLOCK_UINT32_SIZE);
    offset += BLOCK_UINT32_SIZE;
    std::memcpy(&str_result[offset], m_str_metadata.data(), m_str_metadata.length());
    offset += m_str_metadata.length();

    return str_result;
}

std::shared_ptr<CTransaction> CTransaction::Deserialize(const std::string& str_data) {
    size_t n_offset = 0;

    // Minimum size check (version + nonce + from + type + data_len + fee + sig_len + metadata_len)
    const size_t min_size = BLOCK_UINT8_SIZE + BLOCK_UINT64_SIZE + 20 + BLOCK_UINT8_SIZE +
                            BLOCK_UINT32_SIZE + BLOCK_UINT64_SIZE +
                            BLOCK_UINT32_SIZE + BLOCK_UINT32_SIZE;
    if (str_data.length() < min_size) {
        return nullptr;  // Invalid data
    }

    try {
        // Read version (1 byte)
        uint8_t n_version = static_cast<uint8_t>(str_data[n_offset]);
        n_offset += BLOCK_UINT8_SIZE;

        // Read nonce (8 bytes, network byte order)
        uint64_t n_nonce_network;
        std::memcpy(&n_nonce_network, str_data.data() + n_offset, BLOCK_UINT64_SIZE);
        uint64_t n_nonce = be64toh(n_nonce_network);
        n_offset += BLOCK_UINT64_SIZE;

        // Read from address (20 bytes)
        if (n_offset + 20 > str_data.length()) return nullptr;
        std::vector<uint8_t> from_address(str_data.data() + n_offset, str_data.data() + n_offset + 20);
        n_offset += 20;

        // Read tx_type (1 byte)
        uint8_t n_type = static_cast<uint8_t>(str_data[n_offset]);
        TransactionType type = static_cast<TransactionType>(n_type);
        n_offset += BLOCK_UINT8_SIZE;

        // Read data length
        uint32_t n_data_len_network;
        std::memcpy(&n_data_len_network, str_data.data() + n_offset, BLOCK_UINT32_SIZE);
        uint32_t n_data_len = ntohl(n_data_len_network);
        n_offset += BLOCK_UINT32_SIZE;

        // Read data
        if (n_offset + n_data_len > str_data.length()) return nullptr;
        std::vector<uint8_t> data(str_data.data() + n_offset, str_data.data() + n_offset + n_data_len);
        n_offset += n_data_len;

        // Read fee (8 bytes, network byte order)
        uint64_t n_fee_network;
        std::memcpy(&n_fee_network, str_data.data() + n_offset, BLOCK_UINT64_SIZE);
        uint64_t n_fee = be64toh(n_fee_network);
        n_offset += BLOCK_UINT64_SIZE;

        // Read signature length
        uint32_t n_signature_len_network;
        std::memcpy(&n_signature_len_network, str_data.data() + n_offset, BLOCK_UINT32_SIZE);
        uint32_t n_signature_len = ntohl(n_signature_len_network);
        n_offset += BLOCK_UINT32_SIZE;

        // Read signature
        if (n_offset + n_signature_len > str_data.length()) return nullptr;
        std::vector<uint8_t> signature(str_data.data() + n_offset, str_data.data() + n_offset + n_signature_len);
        n_offset += n_signature_len;

        // Read metadata length
        uint32_t n_metadata_len_network;
        std::memcpy(&n_metadata_len_network, str_data.data() + n_offset, BLOCK_UINT32_SIZE);
        uint32_t n_metadata_len = ntohl(n_metadata_len_network);
        n_offset += BLOCK_UINT32_SIZE;

        // Read metadata
        if (n_offset + n_metadata_len > str_data.length()) return nullptr;
        std::string str_metadata(str_data.data() + n_offset, n_metadata_len);

        // Create transaction object using default constructor
        auto p_tx = std::make_shared<CTransaction>();

        // Populate all fields with deserialized values
        p_tx->m_n_version = static_cast<TxVersion>(n_version);
        p_tx->m_n_nonce = n_nonce;
        p_tx->m_from = from_address;
        p_tx->m_type = type;
        p_tx->m_data = data;
        p_tx->m_n_fee = n_fee;
        p_tx->m_signature = signature;
        p_tx->m_n_data_size = data.size();
        p_tx->m_str_metadata = str_metadata;

        // Recompute ID with deserialized fields (without timestamp)
        std::string str_id_input;
        str_id_input += std::to_string(n_version);
        str_id_input += std::to_string(n_nonce);
        str_id_input.insert(str_id_input.end(), from_address.begin(), from_address.end());
        str_id_input += std::to_string(static_cast<uint8_t>(type));
        str_id_input.insert(str_id_input.end(), data.begin(), data.end());
        str_id_input += std::to_string(n_fee);
        str_id_input.insert(str_id_input.end(), signature.begin(), signature.end());
        p_tx->m_id = CHash::ComputeSHA256(str_id_input);

        return p_tx;
    } catch (...) {
        return nullptr;  // Deserialization error
    }
}

/**
 * @brief Convert transaction to JSON string format
 * @return JSON string containing transaction information
 *
 * Returns JSON object with all transaction fields.
 * Binary data is returned as hex string for JSON compatibility.
 */
std::string CTransaction::ToJson() const {
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"transaction_id\": \"" << m_id.GetData() << "\",\n";
    oss << "  \"version\": " << static_cast<int>(m_n_version) << ",\n";
    oss << "  \"nonce\": " << m_n_nonce << ",\n";
    oss << "  \"from\": \"" << BytesToHex(m_from) << "\",\n";
    oss << "  \"type\": \"" << TransactionTypeToString(m_type) << "\",\n";
    oss << "  \"data_hex\": \"" << BytesToHex(m_data) << "\",\n";
    oss << "  \"data_size\": " << m_n_data_size << ",\n";
    oss << "  \"fee\": " << m_n_fee << ",\n";
    oss << "  \"signature\": \"" << BytesToHex(m_signature) << "\",\n";
    oss << "  \"metadata\": \"" << EscapeJsonString(m_str_metadata) << "\"\n";
    oss << "}";

    return oss.str();
}
