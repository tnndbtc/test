// ============= chaininfo_message.h =============
#ifndef CHAININFO_MESSAGE_H
#define CHAININFO_MESSAGE_H

#include "peer/peer_message.h"
#include <cstdint>
#include <memory>
#include <string>

/**
 * @class CChainInfoMessage
 * @brief Type-safe CHAIN_INFO message implementation
 *
 * CHAIN_INFO messages contain blockchain state information.
 * Sent in response to GET_CHAIN requests.
 *
 * Payload format: [height:8bytes][best_block_hash:32bytes]
 *
 * Usage:
 *   CChainInfoMessage chaininfo(100, "abc123...", LOCALNET_MAGIC);
 *   std::string serialized = chaininfo.Serialize();
 */
class CChainInfoMessage : public CPeerMessage {
private:
    uint64_t m_n_height;            ///< Current blockchain height
    std::string m_str_best_block;   ///< Best block hash (hex string)

    /**
     * @brief Convert uint64_t to network byte order
     */
    static uint64_t HostToNetwork64(uint64_t n_value);

    /**
     * @brief Convert uint64_t from network byte order
     */
    static uint64_t NetworkToHost64(uint64_t n_value);

public:
    /**
     * @brief Default constructor with magic
     */
    explicit CChainInfoMessage(uint32_t n_magic);

    /**
     * @brief Constructor with chain data and magic
     */
    CChainInfoMessage(uint64_t n_height, const std::string& str_best_block, uint32_t n_magic);

    /**
     * @brief Get message type (always returns "chain_info")
     */
    std::string GetType() const override;

    /**
     * @brief Serialize chain info to binary payload
     */
    std::vector<uint8_t> SerializePayload() const override;

    /**
     * @brief Deserialize binary payload to chain info
     */
    bool DeserializePayload(const std::vector<uint8_t>& payload) override;

    /**
     * @brief Create a copy of this message
     */
    std::unique_ptr<CPeerMessage> Clone() const override;

    /**
     * @brief Validate message
     */

    /**
     * @brief Get blockchain height
     */
    uint64_t GetHeight() const;

    /**
     * @brief Get best block hash
     */
    const std::string& GetBestBlock() const;

    /**
     * @brief Set blockchain height
     */
    void SetHeight(uint64_t n_height);

    /**
     * @brief Set best block hash
     */
    void SetBestBlock(const std::string& str_best_block);
};

#endif // CHAININFO_MESSAGE_H
