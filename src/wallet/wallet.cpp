// ============= wallet.cpp =============
#include "wallet.h"
#include <random>
#include <sstream>
#include <iomanip>

CWallet::CWallet() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    for(int n_i = 0; n_i < 43; n_i++) {
        ss << std::hex << dis(gen);
    }
    m_str_address = ss.str();
}

std::string CWallet::GetAddress() const {
    return m_str_address;
}

std::shared_ptr<CTransaction> CWallet::CreateTransaction(
    const std::string& str_target,
    const std::vector<uint8_t>& data,
    uint64_t n_reward
) {
    return std::make_shared<CTransaction>(m_str_address, str_target, data, n_reward);
}

