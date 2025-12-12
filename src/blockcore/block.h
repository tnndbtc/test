// ============= block.h =============
#ifndef BLOCK_H
#define BLOCK_H

#include "utils/hash.h"
#include "blockcore/transaction.h"
#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include <mutex>

class CBlock {
    friend class CBlockFile;  // Allow CBlockFile to access private members for serialization

private:
    CHash m_hash;
    CHash m_previous_block;
    int64_t m_n_height;
    int64_t m_n_timestamp;
    std::vector<std::shared_ptr<CTransaction>> m_transactions;
    std::string m_str_miner;
    uint64_t m_n_difficulty;
    uint64_t m_n_cumulative_data_size;
    uint32_t m_n_nonce;
    mutable std::mutex cs_block;  ///< Mutex to protect block data members

public:
    CBlock(const CHash& prev_block, int64_t n_height, const std::string& str_miner);

    /**
     * @brief Create the genesis block (first block in the blockchain)
     * @return Shared pointer to the genesis block
     *
     * Creates a special genesis block with:
     * - Previous block hash: 0x0000...
     * - Height: 0
     * - Miner: "genesis"
     */
    static std::shared_ptr<CBlock> CreateGenesisBlock();

    // Transaction management
    void AddTransaction(std::shared_ptr<CTransaction> tx);

    // Block mining
    void Mine();

    // Getters
    const CHash& GetHash() const { return m_hash; }
    const CHash& GetPreviousBlock() const { return m_previous_block; }
    int64_t GetHeight() const { return m_n_height; }
    int64_t GetTimestamp() const { return m_n_timestamp; }
    std::vector<std::shared_ptr<CTransaction>> GetTransactions() const;
    const std::string& GetMiner() const { return m_str_miner; }
    uint64_t GetDifficulty() const { return m_n_difficulty; }
    uint64_t GetCumulativeDataSize() const { return m_n_cumulative_data_size; }
    uint32_t GetNonce() const { return m_n_nonce; }

    // Utility
    std::string ToString() const;

    /**
     * @brief Convert block to JSON string format
     * @return JSON string containing block information
     *
     * Returns JSON object with all block fields including
     * a full array of transactions. Each transaction uses
     * its own ToJson() method. Used by REST API handlers.
     */
    std::string ToJson() const;

    /**
     * @brief Serialize block to binary format for network transmission
     * @return Binary string containing serialized block data
     */
    std::string Serialize() const;

    /**
     * @brief Deserialize block from binary format
     * @param str_data Binary string containing serialized block
     * @return Shared pointer to deserialized block, or nullptr on error
     */
    static std::shared_ptr<CBlock> Deserialize(const std::string& str_data);
};

#endif
