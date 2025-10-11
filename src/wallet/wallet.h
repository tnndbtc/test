// ============= wallet.h =============
#ifndef WALLET_H
#define WALLET_H

#include "transaction.h"
#include <string>
#include <memory>
#include <vector>
#include <cstdint>

class CWallet {
private:
    std::string m_str_address;

public:
    CWallet();

    std::string GetAddress() const;
    std::shared_ptr<CTransaction> CreateTransaction(
        const std::string& str_target,
        const std::vector<uint8_t>& data,
        uint64_t n_reward = 0
    );
};

#endif

