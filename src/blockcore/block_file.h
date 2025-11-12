// ============= blockfile.h =============
#ifndef BLOCKFILE_H
#define BLOCKFILE_H

#include "blockcore/block.h"
#include "utils/hash.h"
#include <string>
#include <memory>
#include <fstream>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>

/**
 * @class CBlockFile
 * @brief Manages persistent storage of blocks to flat files
 *
 * Provides functionality to save and load blocks from disk in a binary format.
 * Blocks are appended sequentially to .dat files until each file reaches 128MB.
 *
 * Architecture:
 * - Sequential block appending to flat files
 * - Binary serialization format for efficiency
 * - File naming: blk00000.dat, blk00001.dat, blk00002.dat, etc.
 * - Maximum file size: 128MB per file
 * - Index file to track block hash -> (file_number, offset) mapping
 * - Automatic directory creation
 * - Error handling with boolean returns
 *
 * File Format (per block in .dat file):
 * - Block size (4 bytes, uint32_t) - size of following block data
 * - Block hash (32 bytes)
 * - Previous block hash (32 bytes)
 * - Recall block hash (32 bytes)
 * - Height (8 bytes, int64_t)
 * - Timestamp (8 bytes, int64_t)
 * - Difficulty (8 bytes, uint64_t)
 * - Cumulative data size (8 bytes, uint64_t)
 * - Miner address length (4 bytes, uint32_t)
 * - Miner address (variable)
 * - Nonce length (4 bytes, uint32_t)
 * - Nonce (variable)
 * - Transaction count (4 bytes, uint32_t)
 * - For each transaction:
 *   - Transaction ID (32 bytes)
 *   - Owner length (4 bytes) + owner string
 *   - Target length (4 bytes) + target string
 *   - Data size (8 bytes) + data bytes
 *   - Reward (8 bytes, uint64_t)
 *   - Timestamp (8 bytes, int64_t)
 *
 * Index Format (block_index.dat):
 * - Number of entries (4 bytes, uint32_t)
 * - For each entry:
 *   - Block hash (32 bytes)
 *   - File number (4 bytes, uint32_t)
 *   - File offset (8 bytes, uint64_t)
 *
 * Example usage:
 *   CBlockFile blockfile("/path/to/blocks");
 *   blockfile.SaveBlock(block);
 *   auto loaded_block = blockfile.LoadBlock(block_hash);
 */
class CBlockFile {
private:
    static constexpr uint64_t MAX_FILE_SIZE = 128 * 1024 * 1024;  ///< Maximum size per file (128MB)

    std::string m_str_data_dir;  ///< Directory for storing block files
    uint32_t m_n_current_file_number;  ///< Current file number for appending
    uint64_t m_n_current_file_size;    ///< Current size of active file

    /**
     * @struct CBlockIndex
     * @brief Index entry mapping block hash to file location
     */
    struct CBlockIndex {
        std::string str_block_hash;  ///< Block hash as hex string
        uint32_t n_file_number;      ///< File number (blkNNNNN.dat)
        uint64_t n_file_offset;      ///< Offset within the file
    };

    std::unordered_map<std::string, CBlockIndex> map_block_index;  ///< In-memory block index
    mutable std::shared_mutex cs_rw_blockfile;  ///< Read/Write lock for thread-safe file operations

    /**
     * @brief Write a hash to output stream
     * @param ofs Output file stream
     * @param hash Hash to write
     * @return true if successful, false on error
     */
    bool WriteHash(std::ofstream& ofs, const CHash& hash) const;

    /**
     * @brief Read a hash from input stream
     * @param ifs Input file stream
     * @return CHash object read from stream
     */
    CHash ReadHash(std::ifstream& ifs) const;

    /**
     * @brief Write a string to output stream (length-prefixed)
     * @param ofs Output file stream
     * @param str String to write
     * @return true if successful, false on error
     */
    bool WriteString(std::ofstream& ofs, const std::string& str) const;

    /**
     * @brief Read a string from input stream (length-prefixed)
     * @param ifs Input file stream
     * @return String read from stream
     */
    std::string ReadString(std::ifstream& ifs) const;

    /**
     * @brief Write a transaction to output stream
     * @param ofs Output file stream
     * @param p_tx Transaction to write
     * @return true if successful, false on error
     */
    bool WriteTransaction(std::ofstream& ofs, const std::shared_ptr<CTransaction>& p_tx) const;

    /**
     * @brief Read a transaction from input stream
     * @param ifs Input file stream
     * @return Transaction read from stream
     */
    std::shared_ptr<CTransaction> ReadTransaction(std::ifstream& ifs) const;

    /**
     * @brief Get the file path for a block file number
     * @param n_file_number File number
     * @return Full file path for the block file
     */
    std::string GetBlockFilePath(uint32_t n_file_number) const;

    /**
     * @brief Get the index file path
     * @return Full path to block_index.dat
     */
    std::string GetIndexFilePath() const;

    /**
     * @brief Load the block index from disk
     * @return true if successful, false on error
     */
    bool LoadIndex();

    /**
     * @brief Save the block index to disk
     * @return true if successful, false on error
     */
    bool SaveIndex();

    /**
     * @brief Calculate the current file size
     * @return Current file size in bytes
     */
    uint64_t GetCurrentFileSize();

public:
    /**
     * @brief Construct block file manager
     * @param str_data_dir Directory for storing block files
     */
    CBlockFile(const std::string& str_data_dir);

    /**
     * @brief Save a block to disk
     * @param p_block Block to save
     * @return true if successful, false on error
     *
     * Appends the block to the current blkNNNNN.dat file.
     * If current file exceeds 128MB, creates a new file.
     * Updates the block index with location information.
     */
    bool SaveBlock(const std::shared_ptr<CBlock>& p_block);

    /**
     * @brief Load a block from disk
     * @param hash Block hash
     * @return Loaded block, or nullptr on error
     *
     * Uses the block index to find the file and offset,
     * then reads and reconstructs the block object.
     */
    std::shared_ptr<CBlock> LoadBlock(const CHash& hash);

    /**
     * @brief Check if a block exists in the index
     * @param hash Block hash
     * @return true if block exists in index, false otherwise
     */
    bool BlockExists(const CHash& hash) const;

    /**
     * @brief Get the genesis block (height 0) from disk
     * @return Genesis block, or nullptr if not found
     *
     * Scans the index for any block at height 0.
     * This is used during blockchain initialization to avoid
     * recreating genesis with a different random nonce.
     */
    std::shared_ptr<CBlock> GetGenesisBlock();

    /**
     * @brief Get the data directory path
     * @return Data directory path
     */
    const std::string& GetDataDir() const { return m_str_data_dir; }
};

#endif // BLOCKFILE_H
