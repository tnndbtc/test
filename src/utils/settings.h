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

// Maximum number of outbound peer connections
constexpr int MAX_OUTBOUND_PEERS = 8;

// Log directory for daemon logs
const std::string LOG_DIR = "./log";

// Log level (FATAL, ERROR, WARN, INFO, TRACE)
const std::string LOG_LEVEL = "INFO";

#endif // SETTINGS_H
