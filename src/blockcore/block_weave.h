// ============= blockweave.h =============
#ifndef BLOCK_WEAVE_H
#define BLOCK_WEAVE_H

#include "blockcore/block.h"
#include "blockcore/block_file.h"
#include "blockcore/transaction.h"
#include "blockcore/i_block_weave.h"
#include "utils/hash.h"
#include "utils/network.h"
#include "peer/i_peer_manager.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <shared_mutex>
#include <atomic>

class CBlockweave : public IBlockweave {
private:
    std::unordered_map<std::string, std::shared_ptr<CBlock>> map_blocks;
    std::unordered_map<std::string, std::shared_ptr<CBlock>> map_orphan_blocks;  ///< Blocks waiting for parent
    std::vector<CHash> m_block_hashes;
    std::shared_ptr<CBlock> m_genesis_block;
    std::shared_ptr<CBlock> m_current_block;
    std::vector<std::shared_ptr<CTransaction>> m_mempool;

    mutable std::shared_mutex cs_rw_map_blocks;              ///< Read/Write lock for map_blocks
    mutable std::shared_mutex cs_rw_map_orphan_blocks;       ///< Read/Write lock for map_orphan_blocks
    mutable std::shared_mutex cs_rw_m_block_hashes;          ///< Read/Write lock for m_block_hashes
    mutable std::shared_mutex cs_rw_m_mempool;               ///< Read/Write lock for m_mempool

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
    virtual size_t GetOrphanBlocksSize() const override;
    virtual bool HasTransactionInMempool(const std::string& str_tx_hash) const override;
    virtual void SetPeerManager(IPeerManager* p_mgr) override;

    // Additional methods not in interface
    void PrintChain();
    bool ShouldStopMining() const;
    virtual std::shared_ptr<CTransaction> GetTransactionFromMempool(const CHash& tx_hash) const override;
    virtual std::pair<bool, std::vector<std::shared_ptr<CBlock>>> VerifyBlock(std::shared_ptr<CBlock> p_block) override;

    /**
     * @brief Get transaction by hash from mempool or blocks
     * @param tx_hash Transaction hash to search for
     * @return Shared pointer to transaction if found, nullptr otherwise
     *
     * Searches mempool first, then scans all blocks if not found in mempool.
     * Returns the full CTransaction object for use in API responses.
     */
    std::shared_ptr<CTransaction> GetTransaction(const CHash& tx_hash) const;
};

#endif
