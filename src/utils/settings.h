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

// Maximum number of outbound peer connections (we connect to them)
constexpr int MAX_OUTBOUND_PEERS = 8;

// Log directory for daemon logs
const std::string LOG_DIR = "./log";

// Log level (FATAL, ERROR, WARN, INFO, TRACE)
const std::string LOG_LEVEL = "INFO";

// Maximum log file size in megabytes before rotation
constexpr int LOG_FILE_SIZE_MB = 10;

// Number of rotated log files to keep
constexpr int LOG_FILE_KEEP = 5;

#endif // SETTINGS_H
