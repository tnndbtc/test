// ============= blockweave.h =============
#ifndef BLOCKWEAVE_H
#define BLOCKWEAVE_H

#include "blockcore/block.h"
#include "blockcore/transaction.h"
#include "utils/hash.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <atomic>

class CBlockweave {
private:
    std::unordered_map<std::string, std::shared_ptr<CBlock>> map_blocks;
    std::vector<CHash> m_block_hashes;
    std::shared_ptr<CBlock> m_genesis_block;
    std::shared_ptr<CBlock> m_current_block;
    std::vector<std::shared_ptr<CTransaction>> m_mempool;

    mutable std::mutex cs_blockweave;
    std::atomic<bool> f_mining_enabled;
    std::atomic<bool> f_stop_mining;

    CHash SelectRecallBlock(int64_t n_current_height);

public:
    CBlockweave();

    void AddTransaction(std::shared_ptr<CTransaction> tx);
    void MineBlock(const std::string& str_miner_address);
    std::shared_ptr<CBlock> GetBlock(const CHash& hash);
    std::vector<uint8_t> GetData(const CHash& tx_id);
    void PrintChain();

    // Thread control methods
    void StartMining();
    void StopMining();
    bool IsMiningEnabled() const;
    bool ShouldStopMining() const;
    size_t GetMempoolSize() const;
};

#endif
