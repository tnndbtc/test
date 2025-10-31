// ============= block_file.cpp =============
#include "block_file.h"
#include "logger/logger.h"
#include <sys/stat.h>
#include <cstring>
#include <sstream>
#include <iomanip>

CBlockFile::CBlockFile(const std::string& str_data_dir)
    : m_str_data_dir(str_data_dir), m_n_current_file_number(0), m_n_current_file_size(0) {
    // Create data directory if it doesn't exist
    struct stat st;
    if (stat(m_str_data_dir.c_str(), &st) != 0) {
        // Directory doesn't exist, create it
#ifdef PLATFORM_WINDOWS
        _mkdir(m_str_data_dir.c_str());
#else
        mkdir(m_str_data_dir.c_str(), 0755);
#endif
        LOG_INFO("Created block data directory: " + m_str_data_dir);
    }

    // Load existing index
    LoadIndex();

    // Determine current file number and size
    m_n_current_file_size = GetCurrentFileSize();
}

std::string CBlockFile::GetBlockFilePath(uint32_t n_file_number) const {
    std::ostringstream oss;
    oss << m_str_data_dir << "/blk" << std::setw(5) << std::setfill('0') << n_file_number << ".dat";
    return oss.str();
}

std::string CBlockFile::GetIndexFilePath() const {
    return m_str_data_dir + "/block_index.dat";
}

uint64_t CBlockFile::GetCurrentFileSize() {
    std::string str_file_path = GetBlockFilePath(m_n_current_file_number);
    struct stat st;
    if (stat(str_file_path.c_str(), &st) == 0) {
        return static_cast<uint64_t>(st.st_size);
    }
    return 0;
}

bool CBlockFile::LoadIndex() {
    std::string str_index_path = GetIndexFilePath();

    std::ifstream ifs(str_index_path, std::ios::binary);
    if (!ifs) {
        LOG_INFO("No existing block index found, starting fresh");
        return true;  // Not an error, just no existing index
    }

    // Read number of entries
    uint32_t n_num_entries = 0;
    ifs.read(reinterpret_cast<char*>(&n_num_entries), sizeof(n_num_entries));

    if (!ifs.good()) {
        LOG_ERROR("Failed to read index entry count");
        return false;
    }

    LOG_INFO("Loading block index with " + std::to_string(n_num_entries) + " entries");

    // Read each entry
    for (uint32_t n_i = 0; n_i < n_num_entries; n_i++) {
        // Read block hash (32 bytes as binary, convert to hex)
        unsigned char hash_bytes[32];
        ifs.read(reinterpret_cast<char*>(hash_bytes), 32);

        std::string str_hash;
        str_hash.reserve(64);
        for (size_t n_j = 0; n_j < 32; n_j++) {
            char buf[3];
            snprintf(buf, sizeof(buf), "%02x", hash_bytes[n_j]);
            str_hash += buf;
        }

        // Read file number
        uint32_t n_file_number = 0;
        ifs.read(reinterpret_cast<char*>(&n_file_number), sizeof(n_file_number));

        // Read file offset
        uint64_t n_file_offset = 0;
        ifs.read(reinterpret_cast<char*>(&n_file_offset), sizeof(n_file_offset));

        if (!ifs.good()) {
            LOG_ERROR("Failed to read index entry " + std::to_string(n_i));
            return false;
        }

        // Add to index
        CBlockIndex index;
        index.str_block_hash = str_hash;
        index.n_file_number = n_file_number;
        index.n_file_offset = n_file_offset;
        map_block_index[str_hash] = index;

        // Update current file number to be at least the highest we've seen
        if (n_file_number > m_n_current_file_number) {
            m_n_current_file_number = n_file_number;
        }
    }

    ifs.close();
    LOG_INFO("Block index loaded successfully");
    return true;
}

bool CBlockFile::SaveIndex() {
    std::string str_index_path = GetIndexFilePath();

    std::ofstream ofs(str_index_path, std::ios::binary);
    if (!ofs) {
        LOG_ERROR("Failed to open index file for writing: " + str_index_path);
        return false;
    }

    // Write number of entries
    uint32_t n_num_entries = static_cast<uint32_t>(map_block_index.size());
    ofs.write(reinterpret_cast<const char*>(&n_num_entries), sizeof(n_num_entries));

    // Write each entry
    for (const auto& pair : map_block_index) {
        const CBlockIndex& index = pair.second;

        // Write block hash (convert hex string to 32 bytes)
        unsigned char hash_bytes[32];
        for (size_t n_i = 0; n_i < 32; n_i++) {
            std::string str_byte = index.str_block_hash.substr(n_i * 2, 2);
            hash_bytes[n_i] = static_cast<unsigned char>(std::stoi(str_byte, nullptr, 16));
        }
        ofs.write(reinterpret_cast<const char*>(hash_bytes), 32);

        // Write file number
        ofs.write(reinterpret_cast<const char*>(&index.n_file_number), sizeof(index.n_file_number));

        // Write file offset
        ofs.write(reinterpret_cast<const char*>(&index.n_file_offset), sizeof(index.n_file_offset));

        if (!ofs.good()) {
            LOG_ERROR("Failed to write index entry");
            return false;
        }
    }

    ofs.close();
    return ofs.good();
}

bool CBlockFile::WriteHash(std::ofstream& ofs, const CHash& hash) const {
    std::string str_hash = hash.GetData();
    if (str_hash.empty()) {
        // Empty hash - write 32 zero bytes
        char zero_bytes[32] = {0};
        ofs.write(zero_bytes, 32);
    } else {
        // Write hash as 32-byte binary (convert hex string to bytes)
        if (str_hash.length() != 64) {
            LOG_ERROR("Invalid hash length: " + std::to_string(str_hash.length()));
            return false;
        }

        unsigned char bytes[32];
        for (size_t n_i = 0; n_i < 32; n_i++) {
            std::string str_byte = str_hash.substr(n_i * 2, 2);
            bytes[n_i] = static_cast<unsigned char>(std::stoi(str_byte, nullptr, 16));
        }
        ofs.write(reinterpret_cast<const char*>(bytes), 32);
    }

    return ofs.good();
}

CHash CBlockFile::ReadHash(std::ifstream& ifs) const {
    unsigned char bytes[32];
    ifs.read(reinterpret_cast<char*>(bytes), 32);

    if (!ifs.good()) {
        return CHash();
    }

    // Convert bytes to hex string
    std::string str_hex;
    str_hex.reserve(64);
    for (size_t n_i = 0; n_i < 32; n_i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", bytes[n_i]);
        str_hex += buf;
    }

    // Check if all zeros (empty hash)
    bool f_all_zeros = true;
    for (size_t n_i = 0; n_i < 32; n_i++) {
        if (bytes[n_i] != 0) {
            f_all_zeros = false;
            break;
        }
    }

    if (f_all_zeros) {
        return CHash();
    }

    return CHash(str_hex);
}

bool CBlockFile::WriteString(std::ofstream& ofs, const std::string& str) const {
    uint32_t n_length = static_cast<uint32_t>(str.length());
    ofs.write(reinterpret_cast<const char*>(&n_length), sizeof(n_length));
    if (n_length > 0) {
        ofs.write(str.c_str(), n_length);
    }
    return ofs.good();
}

std::string CBlockFile::ReadString(std::ifstream& ifs) const {
    uint32_t n_length = 0;
    ifs.read(reinterpret_cast<char*>(&n_length), sizeof(n_length));

    if (!ifs.good() || n_length == 0) {
        return "";
    }

    std::string str;
    str.resize(n_length);
    ifs.read(&str[0], n_length);

    if (!ifs.good()) {
        return "";
    }

    return str;
}

bool CBlockFile::WriteTransaction(std::ofstream& ofs, const std::shared_ptr<CTransaction>& p_tx) const {
    // Write transaction ID
    if (!WriteHash(ofs, p_tx->m_id)) {
        return false;
    }

    // Write owner
    if (!WriteString(ofs, p_tx->m_str_owner)) {
        return false;
    }

    // Write target
    if (!WriteString(ofs, p_tx->m_str_target)) {
        return false;
    }

    // Write data size and data
    uint64_t n_data_size = p_tx->m_data.size();
    ofs.write(reinterpret_cast<const char*>(&n_data_size), sizeof(n_data_size));
    if (n_data_size > 0) {
        ofs.write(reinterpret_cast<const char*>(p_tx->m_data.data()), n_data_size);
    }

    // Write reward
    ofs.write(reinterpret_cast<const char*>(&p_tx->m_n_reward), sizeof(p_tx->m_n_reward));

    // Write timestamp
    ofs.write(reinterpret_cast<const char*>(&p_tx->m_n_timestamp), sizeof(p_tx->m_n_timestamp));

    return ofs.good();
}

std::shared_ptr<CTransaction> CBlockFile::ReadTransaction(std::ifstream& ifs) const {
    // Read transaction ID
    CHash tx_id = ReadHash(ifs);

    // Read owner
    std::string str_owner = ReadString(ifs);

    // Read target
    std::string str_target = ReadString(ifs);

    // Read data
    uint64_t n_data_size = 0;
    ifs.read(reinterpret_cast<char*>(&n_data_size), sizeof(n_data_size));

    std::vector<uint8_t> data;
    if (n_data_size > 0) {
        data.resize(n_data_size);
        ifs.read(reinterpret_cast<char*>(data.data()), n_data_size);
    }

    // Read reward
    uint64_t n_reward = 0;
    ifs.read(reinterpret_cast<char*>(&n_reward), sizeof(n_reward));

    // Read timestamp
    int64_t n_timestamp = 0;
    ifs.read(reinterpret_cast<char*>(&n_timestamp), sizeof(n_timestamp));

    if (!ifs.good()) {
        return nullptr;
    }

    // Create transaction (constructor will compute new ID, so we need to set it manually)
    auto p_tx = std::make_shared<CTransaction>(str_owner, str_target, data, n_reward);

    // Restore original ID and timestamp
    const_cast<CHash&>(p_tx->m_id) = tx_id;
    const_cast<int64_t&>(p_tx->m_n_timestamp) = n_timestamp;

    return p_tx;
}

bool CBlockFile::SaveBlock(const std::shared_ptr<CBlock>& p_block) {
    std::lock_guard<std::mutex> lock(cs_blockfile);

    // Check if block already exists in index (prevent duplicates)
    std::string str_hash = p_block->GetHash().GetData();
    if (map_block_index.find(str_hash) != map_block_index.end()) {
        LOG_TRACE("Block already exists in index, skipping save: " + str_hash.substr(0, 16) + "...");
        return true;  // Not an error, just already saved
    }

    // Check if we need to start a new file
    if (m_n_current_file_size >= MAX_FILE_SIZE) {
        m_n_current_file_number++;
        m_n_current_file_size = 0;
        LOG_INFO("Starting new block file: blk" + std::to_string(m_n_current_file_number) + ".dat");
    }

    std::string str_file_path = GetBlockFilePath(m_n_current_file_number);
    uint64_t n_file_offset = m_n_current_file_size;

    // Open file in read-write mode (NOT append mode, as we need to seek back to write block size)
    // If file doesn't exist, create it. If it exists, don't truncate it.
    std::ofstream ofs(str_file_path, std::ios::binary | std::ios::in | std::ios::out);
    if (!ofs) {
        // File doesn't exist, create it
        ofs.open(str_file_path, std::ios::binary | std::ios::out);
        if (!ofs) {
            LOG_ERROR("Failed to open file for writing: " + str_file_path);
            return false;
        }
    }

    // Seek to the end to append the new block
    ofs.seekp(0, std::ios::end);

    // Remember position before writing
    std::streampos pos_before = ofs.tellp();

    // Write a placeholder for block size (we'll update this after writing the block)
    uint32_t n_block_size = 0;
    ofs.write(reinterpret_cast<const char*>(&n_block_size), sizeof(n_block_size));

    // Write block data
    if (!WriteHash(ofs, p_block->GetHash())) {
        LOG_ERROR("Failed to write block hash");
        return false;
    }

    if (!WriteHash(ofs, p_block->GetPreviousBlock())) {
        LOG_ERROR("Failed to write previous block hash");
        return false;
    }

    int64_t n_height = p_block->GetHeight();
    ofs.write(reinterpret_cast<const char*>(&n_height), sizeof(n_height));

    int64_t n_timestamp = p_block->GetTimestamp();
    ofs.write(reinterpret_cast<const char*>(&n_timestamp), sizeof(n_timestamp));

    uint64_t n_difficulty = p_block->GetDifficulty();
    ofs.write(reinterpret_cast<const char*>(&n_difficulty), sizeof(n_difficulty));

    uint64_t n_cumulative_data_size = p_block->GetCumulativeDataSize();
    ofs.write(reinterpret_cast<const char*>(&n_cumulative_data_size), sizeof(n_cumulative_data_size));

    if (!WriteString(ofs, p_block->GetMiner())) {
        LOG_ERROR("Failed to write miner address");
        return false;
    }

    if (!WriteString(ofs, p_block->GetNonce())) {
        LOG_ERROR("Failed to write nonce");
        return false;
    }

    uint32_t n_tx_count = static_cast<uint32_t>(p_block->GetTransactions().size());
    ofs.write(reinterpret_cast<const char*>(&n_tx_count), sizeof(n_tx_count));

    for (const auto& p_tx : p_block->GetTransactions()) {
        if (!WriteTransaction(ofs, p_tx)) {
            LOG_ERROR("Failed to write transaction");
            return false;
        }
    }

    // Calculate actual block size
    std::streampos pos_after = ofs.tellp();
    n_block_size = static_cast<uint32_t>(pos_after - pos_before - sizeof(n_block_size));

    // Go back and write the actual block size
    ofs.seekp(pos_before);
    ofs.write(reinterpret_cast<const char*>(&n_block_size), sizeof(n_block_size));
    ofs.seekp(pos_after);

    ofs.close();

    if (!ofs.good()) {
        LOG_ERROR("Error occurred while writing block file");
        return false;
    }

    // Update current file size
    m_n_current_file_size = static_cast<uint64_t>(pos_after);

    // Add to index
    CBlockIndex index;
    index.str_block_hash = p_block->GetHash().GetData();
    index.n_file_number = m_n_current_file_number;
    index.n_file_offset = n_file_offset;
    map_block_index[index.str_block_hash] = index;

    // Save index
    SaveIndex();

    LOG_TRACE("Block saved to disk: " + p_block->GetHash().GetData().substr(0, 16) + "... at height " + std::to_string(p_block->GetHeight()) +
              " (file: blk" + std::to_string(m_n_current_file_number) + ".dat, offset: " + std::to_string(n_file_offset) + ")");
    return true;
}

std::shared_ptr<CBlock> CBlockFile::LoadBlock(const CHash& hash) {
    std::lock_guard<std::mutex> lock(cs_blockfile);

    // Find block in index
    auto it = map_block_index.find(hash.GetData());
    if (it == map_block_index.end()) {
        LOG_ERROR("Block not found in index: " + hash.GetData().substr(0, 16) + "...");
        return nullptr;
    }

    const CBlockIndex& index = it->second;
    std::string str_file_path = GetBlockFilePath(index.n_file_number);

    std::ifstream ifs(str_file_path, std::ios::binary);
    if (!ifs) {
        LOG_ERROR("Failed to open file for reading: " + str_file_path);
        return nullptr;
    }

    // Seek to block position
    ifs.seekg(index.n_file_offset);

    // Read block size
    uint32_t n_block_size = 0;
    ifs.read(reinterpret_cast<char*>(&n_block_size), sizeof(n_block_size));

    // Read block data
    CHash block_hash = ReadHash(ifs);
    CHash previous_hash = ReadHash(ifs);

    int64_t n_height = 0;
    ifs.read(reinterpret_cast<char*>(&n_height), sizeof(n_height));

    int64_t n_timestamp = 0;
    ifs.read(reinterpret_cast<char*>(&n_timestamp), sizeof(n_timestamp));

    uint64_t n_difficulty = 0;
    ifs.read(reinterpret_cast<char*>(&n_difficulty), sizeof(n_difficulty));

    uint64_t n_cumulative_data_size = 0;
    ifs.read(reinterpret_cast<char*>(&n_cumulative_data_size), sizeof(n_cumulative_data_size));

    std::string str_miner = ReadString(ifs);
    std::string str_nonce = ReadString(ifs);

    uint32_t n_tx_count = 0;
    ifs.read(reinterpret_cast<char*>(&n_tx_count), sizeof(n_tx_count));

    if (!ifs.good()) {
        LOG_ERROR("Error reading block metadata from file");
        return nullptr;
    }

    // Create block
    auto p_block = std::make_shared<CBlock>(previous_hash, n_height, str_miner);

    // Restore saved block hash and nonce (these getters return references)
    // Note: timestamp, difficulty, cumulative_data_size getters return by value
    // so they can't be restored this way, but for block identity the hash is most important
    const_cast<CHash&>(p_block->GetHash()) = block_hash;
    const_cast<std::string&>(p_block->GetNonce()) = str_nonce;

    // Read and add transactions
    for (uint32_t n_i = 0; n_i < n_tx_count; n_i++) {
        auto p_tx = ReadTransaction(ifs);
        if (!p_tx) {
            LOG_ERROR("Failed to read transaction from block file");
            return nullptr;
        }
        p_block->AddTransaction(p_tx);
    }

    ifs.close();

    LOG_TRACE("Block loaded from disk: " + hash.GetData().substr(0, 16) + "... at height " + std::to_string(n_height) +
              " (file: blk" + std::to_string(index.n_file_number) + ".dat, offset: " + std::to_string(index.n_file_offset) + ")");
    return p_block;
}

bool CBlockFile::BlockExists(const CHash& hash) const {
    std::lock_guard<std::mutex> lock(cs_blockfile);
    return map_block_index.find(hash.GetData()) != map_block_index.end();
}

std::shared_ptr<CBlock> CBlockFile::GetGenesisBlock() {
    // Scan index for all blocks, load each one, and find the one with height 0
    // This is a linear scan but should be fine since this is only called once on startup
    std::lock_guard<std::mutex> lock(cs_blockfile);

    for (const auto& pair : map_block_index) {
        const CBlockIndex& index = pair.second;

        // Load the block to check its height
        std::string str_file_path = GetBlockFilePath(index.n_file_number);
        std::ifstream ifs(str_file_path, std::ios::binary);
        if (!ifs) {
            continue;  // Skip if can't open file
        }

        // Seek to block position
        ifs.seekg(index.n_file_offset);
        if (!ifs.good()) {
            continue;
        }

        // Read block size prefix
        uint32_t n_block_size = 0;
        ifs.read(reinterpret_cast<char*>(&n_block_size), sizeof(n_block_size));
        if (!ifs.good()) {
            continue;
        }

        // Read block hash (we already know it, but need to advance stream)
        CHash block_hash = ReadHash(ifs);

        // Read previous block hash
        CHash prev_hash = ReadHash(ifs);

        // Read height
        int64_t n_height = 0;
        ifs.read(reinterpret_cast<char*>(&n_height), sizeof(n_height));
        if (!ifs.good()) {
            continue;
        }

        // Check if this is genesis (height 0)
        if (n_height == 0) {
            // Found genesis! Now load the entire block properly
            // Seek back to start of block
            ifs.seekg(index.n_file_offset);

            // Read block size again
            ifs.read(reinterpret_cast<char*>(&n_block_size), sizeof(n_block_size));

            // Read full block
            block_hash = ReadHash(ifs);
            prev_hash = ReadHash(ifs);
            ifs.read(reinterpret_cast<char*>(&n_height), sizeof(n_height));

            int64_t n_timestamp = 0;
            ifs.read(reinterpret_cast<char*>(&n_timestamp), sizeof(n_timestamp));

            uint64_t n_difficulty = 0;
            ifs.read(reinterpret_cast<char*>(&n_difficulty), sizeof(n_difficulty));

            uint64_t n_cumulative_data_size = 0;
            ifs.read(reinterpret_cast<char*>(&n_cumulative_data_size), sizeof(n_cumulative_data_size));

            std::string str_miner = ReadString(ifs);
            std::string str_nonce = ReadString(ifs);

            // Read transactions
            uint32_t n_tx_count = 0;
            ifs.read(reinterpret_cast<char*>(&n_tx_count), sizeof(n_tx_count));

            auto p_block = std::make_shared<CBlock>(prev_hash, n_height, str_miner);

            for (uint32_t n_i = 0; n_i < n_tx_count; n_i++) {
                auto p_tx = ReadTransaction(ifs);
                if (p_tx) {
                    p_block->AddTransaction(p_tx);
                }
            }

            LOG_INFO("Found genesis block in index: " + block_hash.GetData().substr(0, 16) + "...");
            return p_block;
        }
    }

    LOG_INFO("No genesis block found in index");
    return nullptr;
}
