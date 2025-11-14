// ============= network.h =============
#ifndef NETWORK_H
#define NETWORK_H

#include <string>
#include <cstdint>

/**
 * @enum NetworkType
 * @brief Enumeration of supported network types
 */
enum class NetworkType : uint8_t {
    MAINNET = 0,  ///< Production network
    TESTNET = 1,  ///< Testing network with relaxed rules
    LOCALNET = 2  ///< Local development network
};

// Network magic bytes for P2P message validation
// These ensure nodes on different networks cannot communicate
constexpr uint32_t MAINNET_MAGIC = 0x8AC65DF3;
constexpr uint32_t TESTNET_MAGIC = 0xEA71B96E;
constexpr uint32_t LOCALNET_MAGIC = 0xACDE4892;

/**
 * @struct NetworkParams
 * @brief Network-specific configuration parameters
 */
struct NetworkParams {
    NetworkType type;                    ///< Network type identifier
    uint32_t magic_bytes;                ///< P2P protocol magic bytes
    std::string network_name;            ///< Human-readable network name
    int default_p2p_port;                ///< Default P2P listening port
    int default_rest_port;               ///< Default REST API port
    uint64_t genesis_timestamp;          ///< Genesis block timestamp
    uint32_t genesis_nonce;              ///< Genesis block nonce
    std::string genesis_hash_prefix;     ///< Expected difficulty prefix for genesis
    // int target_block_time_seconds;       ///< Target time between blocks
    bool fast_mining;                    ///< Skip difficulty check (localnet only)
};

/**
 * @brief Get network parameters for specified network type
 * @param type Network type
 * @return Reference to network parameters
 * @throws std::runtime_error if network type is invalid
 */
const NetworkParams& GetNetworkParams(NetworkType type);

/**
 * @brief Parse network type from string
 * @param str_network Network name (e.g., "mainnet", "testnet", "localnet")
 * @return NetworkType enum value
 * @throws std::runtime_error if network string is invalid
 *
 * Case-insensitive parsing. Valid values: mainnet, testnet, localnet
 */
NetworkType ParseNetworkType(const std::string& str_network);

/**
 * @brief Convert network type to string representation
 * @param type Network type
 * @return Network name as string
 */
std::string NetworkTypeToString(NetworkType type);

#endif // NETWORK_H
