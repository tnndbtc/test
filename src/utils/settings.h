// ============= settings.h =============
#ifndef SETTINGS_H
#define SETTINGS_H

#include <string>

// ============================================================
// Global Settings and Constants
// ============================================================

// Number of worker threads for REST API request processing
constexpr int REST_WORKER_THREADS = 5;

// REST API server port
constexpr int REST_API_PORT = 28443;

// Peer-to-peer network port
constexpr int P2P_PORT = 28333;

// Maximum number of inbound peer connections (peers connecting to us)
constexpr int MAX_INBOUND_PEERS = 120;

// Maximum number of inbound worker threads for message processing
constexpr int MAX_INBOUND_WORKER_THREADS = 32;

// Maximum number of outbound peer connections (we connect to them)
constexpr int MAX_OUTBOUND_PEERS = 8;

// Interval in seconds between sending PING messages to peers for keep-alive
constexpr int PEERS_PING_TIME = 180;

// Peer rotation interval in seconds (30 minutes)
constexpr int PEER_ROTATION_INTERVAL = 1800;

// Misbehavior score threshold for banning peers
constexpr int PEER_BAN_THRESHOLD = 100;

// Ban duration in seconds (24 hours)
constexpr int PEER_BAN_DURATION = 86400;

// Log directory for daemon logs
const std::string LOG_DIR = "./log";

// Log level (FATAL, ERROR, WARN, INFO, TRACE)
const std::string LOG_LEVEL = "INFO";

// Maximum log file size in megabytes before rotation
constexpr int LOG_FILE_SIZE_MB = 10;

// Number of rotated log files to keep
constexpr int LOG_FILE_KEEP = 5;

// ============================================================
// Mining Configuration
// ============================================================

// Mining interval in seconds when mempool has transactions
constexpr int MINING_INTERVAL_SECONDS = 1;

// Mining interval in seconds when mempool is empty (24 hours)
constexpr int MINING_EMPTY_BLOCK_INTERVAL_SECONDS = 86400;

// ============================================================
// P2P Message Format Constants
// ============================================================

// Size of count field in inventory/GETDATA messages (uint32_t, network byte order)
constexpr size_t MESSAGE_COUNT_SIZE = 4;

// Size of object type field in inventory/GETDATA messages (ObjectType::Type, uint16, network byte order)
constexpr size_t MESSAGE_TYPE_SIZE = 2;

// Size of binary hash in inventory/GETDATA messages (SHA-256, 32 bytes)
constexpr size_t MESSAGE_HASH_SIZE = 32;

// Size of hexadecimal hash string representation (SHA-256 as hex, 64 characters)
constexpr size_t MESSAGE_HASH_HEX_SIZE = 64;

// ============================================================
// Block and Transaction Serialization Field Sizes
// ============================================================

// Size of hash field in block serialization (SHA-256, 32 bytes)
constexpr size_t BLOCK_HASH_SIZE = 32;

// Size of int64_t fields (height, timestamp) in network byte order
constexpr size_t BLOCK_INT64_SIZE = 8;

// Size of uint64_t fields (difficulty, cumulative_data_size, reward) in network byte order
constexpr size_t BLOCK_UINT64_SIZE = 8;

// Size of uint32_t length fields (string length, count) in network byte order
constexpr size_t BLOCK_UINT32_SIZE = 4;

// Size of uint8_t fields (transaction type)
constexpr size_t BLOCK_UINT8_SIZE = 1;

#endif // SETTINGS_H
