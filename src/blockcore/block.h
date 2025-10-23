// ============= block.h =============
#ifndef BLOCK_H
#define BLOCK_H

#include "utils/hash.h"
#include "blockcore/transaction.h"
#include <vector>
#include <memory>
#include <string>
#include <cstdint>

class CBlock {
private:
    CHash m_hash;
    CHash m_previous_block;
    int64_t m_n_height;
    int64_t m_n_timestamp;
    std::vector<std::shared_ptr<CTransaction>> m_transactions;
    std::string m_str_miner;
    uint64_t m_n_difficulty;
    uint64_t m_n_cumulative_data_size;
    std::string m_str_nonce;

public:
    CBlock(const CHash& prev_block, int64_t n_height, const std::string& str_miner);

    // Transaction management
    void AddTransaction(std::shared_ptr<CTransaction> tx);

    // Block mining
    void Mine();

    // Getters
    const CHash& GetHash() const { return m_hash; }
    const CHash& GetPreviousBlock() const { return m_previous_block; }
    int64_t GetHeight() const { return m_n_height; }
    int64_t GetTimestamp() const { return m_n_timestamp; }
    const std::vector<std::shared_ptr<CTransaction>>& GetTransactions() const { return m_transactions; }
    const std::string& GetMiner() const { return m_str_miner; }
    uint64_t GetDifficulty() const { return m_n_difficulty; }
    uint64_t GetCumulativeDataSize() const { return m_n_cumulative_data_size; }
    const std::string& GetNonce() const { return m_str_nonce; }

    // Utility
    std::string ToString() const;
};

#endif
