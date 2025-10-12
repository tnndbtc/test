// ============= block.h =============
#ifndef BLOCK_H
#define BLOCK_H

#include "utils/hash.h"
#include "transaction.h"
#include <vector>
#include <memory>
#include <string>
#include <cstdint>

class CBlock {
public:
    CHash m_hash;
    CHash m_previous_block;
    CHash m_recall_block;
    int64_t m_n_height;
    int64_t m_n_timestamp;
    std::vector<std::shared_ptr<CTransaction>> m_transactions;
    std::string m_str_miner;
    uint64_t m_n_difficulty;
    uint64_t m_n_cumulative_data_size;
    std::string m_str_nonce;

    CBlock(const CHash& prev_block, int64_t n_height, const std::string& str_miner);

    void AddTransaction(std::shared_ptr<CTransaction> tx);
    void SetRecallBlock(const CHash& recall);
    void Mine();
    std::string ToString() const;
};

#endif

