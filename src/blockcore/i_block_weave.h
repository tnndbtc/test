// ============= i_block_weave.h =============
#ifndef I_BLOCK_WEAVE_H
#define I_BLOCK_WEAVE_H

#include "blockcore/block.h"
#include "blockcore/transaction.h"
#include "utils/hash.h"
#include <memory>
#include <vector>
#include <string>

// Forward declaration to avoid circular dependency
class IPeerManager;

/**
 * @interface IBlockweave
 * @brief Interface for blockweave blockchain operations
 *
 * Provides abstraction for blockchain core functionality including:
 * - Transaction and block management
 * - Mining control
 * - Mempool queries
 * - Peer manager integration
 *
 * This interface allows peer_manager to interact with blockweave
 * without tight coupling to the concrete implementation.
 */
class IBlockweave {
public:
    /**
     * @brief Virtual destructor for proper cleanup
     */
    virtual ~IBlockweave() = default;

    /**
     * @brief Add transaction to mempool
     * @param tx Shared pointer to transaction
     */
    virtual void AddTransaction(std::shared_ptr<CTransaction> tx) = 0;

    /**
     * @brief Mine a new block
     * @param str_miner_address Address to receive mining reward
     */
    virtual void MineBlock(const std::string& str_miner_address) = 0;

    /**
     * @brief Get block by hash
     * @param hash Block hash
     * @return Shared pointer to block, or nullptr if not found
     */
    virtual std::shared_ptr<CBlock> GetBlock(const CHash& hash) = 0;

    /**
     * @brief Get transaction data by ID
     * @param tx_id Transaction ID hash
     * @return Transaction data as byte vector
     */
    virtual std::vector<uint8_t> GetData(const CHash& tx_id) = 0;

    /**
     * @brief Start mining thread
     */
    virtual void StartMining() = 0;

    /**
     * @brief Stop mining thread
     */
    virtual void StopMining() = 0;

    /**
     * @brief Check if mining is enabled
     * @return true if mining is enabled
     */
    virtual bool IsMiningEnabled() const = 0;

    /**
     * @brief Get number of transactions in mempool
     * @return Mempool size
     */
    virtual size_t GetMempoolSize() const = 0;

    /**
     * @brief Check if transaction exists in mempool
     * @param str_tx_hash Transaction hash (hex string)
     * @return true if transaction is in mempool
     */
    virtual bool HasTransactionInMempool(const std::string& str_tx_hash) const = 0;

    /**
     * @brief Get transaction from mempool by hash
     * @param tx_hash Transaction hash
     * @return Shared pointer to transaction, or nullptr if not found
     */
    virtual std::shared_ptr<CTransaction> GetTransactionFromMempool(const CHash& tx_hash) const = 0;

    /**
     * @brief Set peer manager for network broadcasting
     * @param p_mgr Pointer to peer manager
     */
    virtual void SetPeerManager(IPeerManager* p_mgr) = 0;
};

#endif // I_BLOCK_WEAVE_H
