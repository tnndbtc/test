// ============= transaction_payload.cpp =============
/**
 * @file transaction_payload.cpp
 * @brief Implementation of transaction payload structures
 */

#include "transaction_payload.h"
#include "utils/serialization.h"
#include "utils/address.h"
#include <sstream>

// Address length constant
constexpr size_t PAYLOAD_ADDRESS_LENGTH = 20;

CTransferPayload::CTransferPayload(const std::vector<uint8_t>& to, uint64_t n_amount)
    : m_to(to), m_n_amount(n_amount) {
}

CTransferPayload::CTransferPayload()
    : m_to(), m_n_amount(0) {
}

std::vector<uint8_t> CTransferPayload::Serialize() const {
    std::vector<uint8_t> result;

    // Write 'to' address (20 bytes, no length prefix)
    std::vector<uint8_t> to_bytes = WriteBytes(m_to, PAYLOAD_ADDRESS_LENGTH);
    result.insert(result.end(), to_bytes.begin(), to_bytes.end());

    // Write amount (8 bytes, little-endian)
    std::vector<uint8_t> amount_bytes = WriteUint64LE(m_n_amount);
    result.insert(result.end(), amount_bytes.begin(), amount_bytes.end());

    return result;
}

std::shared_ptr<CTransferPayload> CTransferPayload::Deserialize(const std::vector<uint8_t>& data) {
    // Expected size: 20 bytes (address) + 8 bytes (amount) = 28 bytes
    constexpr size_t EXPECTED_SIZE = PAYLOAD_ADDRESS_LENGTH + 8;

    if (data.size() != EXPECTED_SIZE) {
        return nullptr;  // Invalid size
    }

    const uint8_t* ptr = data.data();
    size_t n_remaining = data.size();

    // Read 'to' address (20 bytes)
    std::vector<uint8_t> to;
    if (!ReadBytes(ptr, n_remaining, PAYLOAD_ADDRESS_LENGTH, to)) {
        return nullptr;
    }

    // Read amount (8 bytes)
    uint64_t n_amount;
    if (!ReadUint64LE(ptr, n_remaining, n_amount)) {
        return nullptr;
    }

    return std::make_shared<CTransferPayload>(to, n_amount);
}

std::string CTransferPayload::ToJson() const {
    std::ostringstream oss;
    oss << "\"to\": \"" << AddressToHex(m_to) << "\", ";
    oss << "\"amount\": " << m_n_amount;
    return oss.str();
}
