// ============= settings.h =============
#ifndef SETTINGS_H
#define SETTINGS_H

#include <string>

// ============================================================
// Global Settings and Constants
// ============================================================

// Number of worker threads for REST API request processing
constexpr int WORKER_THREADS = 5;

// REST API server port
constexpr int REST_API_PORT = 28443;

// Log directory for daemon logs
const std::string LOG_DIR = "./log";

// Log level (FATAL, ERROR, WARN, INFO, TRACE)
const std::string LOG_LEVEL = "INFO";

#endif // SETTINGS_H
