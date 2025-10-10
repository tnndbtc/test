// ============= hash.cpp =============
#include "hash.h"
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>

CHash::CHash() : m_str_data(64, '0') {}

CHash::CHash(const std::string& input) {
    unsigned char hash_buffer[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.length(), hash_buffer);

    std::stringstream ss;
    for(int n_i = 0; n_i < SHA256_DIGEST_LENGTH; n_i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash_buffer[n_i]);
    }
    m_str_data = ss.str();
}

bool CHash::operator==(const CHash& other) const {
    return m_str_data == other.m_str_data;
}

bool CHash::operator<(const CHash& other) const {
    return m_str_data < other.m_str_data;
}

