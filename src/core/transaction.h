// ============= transaction.h =============
#ifndef TRANSACTION_H
#define TRANSACTION_H

#include "utils/hash.h"
#include <string>
#include <vector>
#include <cstdint>

struct CTransaction {
    CHash m_id;
    std::string m_str_owner;
    std::string m_str_target;
    std::vector<uint8_t> m_data;
    size_t m_n_data_size;
    uint64_t m_n_reward;
    int64_t m_n_timestamp;

    CTransaction(const std::string& str_owner, const std::string& str_target,
                 const std::vector<uint8_t>& data, uint64_t n_reward);
};

#endif
