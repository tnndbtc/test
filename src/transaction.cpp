// ============= transaction.cpp =============
#include "transaction.h"
#include <chrono>

CTransaction::CTransaction(const std::string& str_owner, const std::string& str_target,
                           const std::vector<uint8_t>& data, uint64_t n_reward)
    : m_str_owner(str_owner), m_str_target(str_target), m_data(data),
      m_n_data_size(data.size()), m_n_reward(n_reward) {
    m_n_timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    m_id = CHash(str_owner + str_target + std::to_string(m_n_timestamp));
}

