// ============= network.cpp =============
#include "network.h"
#include <stdexcept>
#include <algorithm>

// Network parameter definitions
static const NetworkParams MAINNET_PARAMS = {
    NetworkType::MAINNET,
    MAINNET_MAGIC,
    "mainnet",
    28333,              // P2P port
    28443,              // REST port
    1762553229520435,   // Genesis timestamp (from existing genesis block)
    1106191456,         // Genesis nonce
    "0fff",             // Difficulty target prefix (12 leading zero bits)
    // 600,                // 10 minutes between blocks
    false               // Normal mining with difficulty
};

static const NetworkParams TESTNET_PARAMS = {
    NetworkType::TESTNET,
    TESTNET_MAGIC,
    "testnet",
    38333,              // Different P2P port to avoid conflicts
    38443,              // Different REST port
    1762553229520435,   // Same genesis for compatibility
    1106191456,
    "0fff",
    // 60,                 // 1 minute blocks for faster testing
    false
};

static const NetworkParams LOCALNET_PARAMS = {
    NetworkType::LOCALNET,
    LOCALNET_MAGIC,
    "localnet",
    48333,              // Different P2P port
    48443,              // Different REST port
    1762553229520435,
    1106191456,
    "ffff",             // No difficulty requirement
    // 1,                  // 1 second blocks (but mining disabled by default)
    true                // Fast mining mode - bypass difficulty
};

const NetworkParams& GetNetworkParams(NetworkType type) {
    switch (type) {
        case NetworkType::MAINNET:
            return MAINNET_PARAMS;
        case NetworkType::TESTNET:
            return TESTNET_PARAMS;
        case NetworkType::LOCALNET:
            return LOCALNET_PARAMS;
        default:
            throw std::runtime_error("Invalid network type");
    }
}

NetworkType ParseNetworkType(const std::string& str_network) {
    std::string str_lower = str_network;
    std::transform(str_lower.begin(), str_lower.end(), str_lower.begin(), ::tolower);

    if (str_lower == "mainnet") return NetworkType::MAINNET;
    if (str_lower == "testnet") return NetworkType::TESTNET;
    if (str_lower == "localnet") return NetworkType::LOCALNET;

    throw std::runtime_error("Invalid network type: " + str_network +
                            " (valid options: mainnet, testnet, localnet)");
}

std::string NetworkTypeToString(NetworkType type) {
    switch (type) {
        case NetworkType::MAINNET: return "mainnet";
        case NetworkType::TESTNET: return "testnet";
        case NetworkType::LOCALNET: return "localnet";
        default: return "unknown";
    }
}
