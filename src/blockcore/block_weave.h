// ============= blockweave.h =============
#ifndef BLOCK_WEAVE_H
#define BLOCK_WEAVE_H

#include "blockcore/block.h"
#include "blockcore/block_file.h"
#include "blockcore/transaction.h"
#include "blockcore/i_block_weave.h"
#include "utils/hash.h"
#include "peer/i_peer_manager.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <atomic>

class CBlockweave : public IBlockweave {
private:
    std::unordered_map<std::string, std::shared_ptr<CBlock>> map_blocks;
    std::vector<CHash> m_block_hashes;
    std::shared_ptr<CBlock> m_genesis_block;
    std::shared_ptr<CBlock> m_current_block;
    std::vector<std::shared_ptr<CTransaction>> m_mempool;

    mutable std::mutex cs_blockweave;
    std::atomic<bool> f_mining_enabled;
    std::atomic<bool> f_stop_mining;

    IPeerManager* p_peer_manager;  ///< Pointer to peer manager for broadcasting (optional)
    std::unique_ptr<CBlockFile> m_p_blockfile;  ///< Block file manager for persistent storage

public:
    CBlockweave();
    CBlockweave(const std::string& str_data_dir);
    virtual ~CBlockweave();

    // IBlockweave interface implementation
    virtual void AddTransaction(std::shared_ptr<CTransaction> tx) override;
    virtual void MineBlock(const std::string& str_miner_address) override;
    virtual std::shared_ptr<CBlock> GetBlock(const CHash& hash) override;
    virtual std::vector<uint8_t> GetData(const CHash& tx_id) override;
    virtual void StartMining() override;
    virtual void StopMining() override;
    virtual bool IsMiningEnabled() const override;
    virtual size_t GetMempoolSize() const override;
    virtual size_t GetBlockCount() const override;
    virtual bool HasTransactionInMempool(const std::string& str_tx_hash) const override;
    virtual void SetPeerManager(IPeerManager* p_mgr) override;

    // Additional methods not in interface
    void PrintChain();
    bool ShouldStopMining() const;
    virtual std::shared_ptr<CTransaction> GetTransactionFromMempool(const CHash& tx_hash) const override;
};

#endif
