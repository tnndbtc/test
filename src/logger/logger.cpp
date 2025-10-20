// ============= logger.cpp =============
/**
 * @file logger.cpp
 * @brief Implementation of thread-safe logging system
 *
 * Provides implementation of CLogger class for writing timestamped,
 * leveled log messages to file with optional console output.
 */

#include "logger.h"
#include "utils/threadname.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __APPLE__
#include <libproc.h>
#endif

/// Global logger instance (initialized by InitializeLogger)
std::shared_ptr<CLogger> g_p_logger = nullptr;

CLogger::CLogger() : f_initialized(false), m_min_log_level(ELogLevel::INFO) {
}

CLogger::~CLogger() {
    if (m_log_stream.is_open()) {
        m_log_stream.flush();
        m_log_stream.close();
    }
}

/**
 * @brief Generate current timestamp string with millisecond precision
 * @return Formatted timestamp string "YYYY-MM-DD HH:MM:SS.mmm"
 *
 * Uses system clock to generate timestamp in local time with
 * millisecond precision for accurate log timing.
 */
std::string CLogger::GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << now_ms.count();
    return ss.str();
}

/**
 * @brief Convert log level enum to fixed-width string
 * @param level Log level to convert
 * @return 5-character string representation (padded for alignment)
 *
 * Returns fixed-width strings for consistent log formatting.
 */
std::string CLogger::GetLevelString(ELogLevel level) {
    switch (level) {
        case ELogLevel::TRACE:
            return "TRACE";
        case ELogLevel::INFO:
            return "INFO ";
        case ELogLevel::WARN:
            return "WARN ";
        case ELogLevel::ERROR:
            return "ERROR";
        case ELogLevel::FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Get current process name
 * @return Process name as string
 *
 * Platform-specific implementation to retrieve the current process name.
 * On macOS, uses proc_pidpath() to get the full path then extracts basename.
 * On Linux, reads from /proc/self/comm or /proc/self/cmdline.
 */
std::string CLogger::GetProcessName() {
#ifdef __APPLE__
    char path_buf[PROC_PIDPATHINFO_MAXSIZE];
    pid_t pid = getpid();
    int ret = proc_pidpath(pid, path_buf, sizeof(path_buf));
    if (ret <= 0) {
        return "unknown";
    }
    // Extract basename from full path
    std::string str_full_path(path_buf);
    size_t n_last_slash = str_full_path.find_last_of('/');
    if (n_last_slash != std::string::npos) {
        return str_full_path.substr(n_last_slash + 1);
    }
    return str_full_path;
#elif defined(__linux__)
    // Try /proc/self/comm first (more reliable for process name)
    std::ifstream comm_file("/proc/self/comm");
    if (comm_file.is_open()) {
        std::string str_name;
        std::getline(comm_file, str_name);
        if (!str_name.empty()) {
            return str_name;
        }
    }
    // Fallback to cmdline
    std::ifstream cmdline_file("/proc/self/cmdline");
    if (cmdline_file.is_open()) {
        std::string str_cmdline;
        std::getline(cmdline_file, str_cmdline, '\0');
        size_t n_last_slash = str_cmdline.find_last_of('/');
        if (n_last_slash != std::string::npos) {
            return str_cmdline.substr(n_last_slash + 1);
        }
        return str_cmdline;
    }
    return "unknown";
#else
    return "unknown";
#endif
}

/**
 * @brief Get current thread name as string
 * @return Thread name as string, or thread ID if name not set
 *
 * Delegates to the centralized thread naming utility in utils/threadname.h
 * which provides cross-platform thread name retrieval.
 */
std::string CLogger::GetThreadName() {
    return ::GetThreadName();
}

/**
 * @brief Parse log level from string (case-insensitive)
 * @param str_level String to parse (e.g., "info", "ERROR", "warning")
 * @return Corresponding ELogLevel, defaults to INFO if unrecognized
 *
 * Accepts common variations like "WARN" or "WARNING".
 * Unrecognized strings default to INFO level.
 */
ELogLevel CLogger::ParseLogLevel(const std::string& str_level) {
    std::string str_upper = str_level;
    // Convert to uppercase for case-insensitive comparison
    for (char& c : str_upper) {
        c = std::toupper(c);
    }

    if (str_upper == "TRACE") return ELogLevel::TRACE;
    if (str_upper == "INFO") return ELogLevel::INFO;
    if (str_upper == "WARN" || str_upper == "WARNING") return ELogLevel::WARN;
    if (str_upper == "ERROR") return ELogLevel::ERROR;
    if (str_upper == "FATAL") return ELogLevel::FATAL;

    // Default to INFO if unknown
    return ELogLevel::INFO;
}

/**
 * @brief Initialize logger with directory and minimum level
 * @param str_log_dir Directory for log files (created if doesn't exist)
 * @param min_level Minimum log level to record
 * @return true on success, false on failure
 *
 * Creates timestamped log file in format: rest_daemon_YYYYMMDD_HHMMSS.log
 * Creates log directory if it doesn't exist. Thread-safe.
 */
bool CLogger::Initialize(const std::string& str_log_dir, ELogLevel min_level) {
    std::lock_guard<std::recursive_mutex> lock(cs_log);

    m_str_log_dir = str_log_dir;
    m_min_log_level = min_level;

    // Create log directory if it doesn't exist
    struct stat st;
    if (stat(str_log_dir.c_str(), &st) != 0) {
        // Directory doesn't exist, try to create it
        if (mkdir(str_log_dir.c_str(), 0755) != 0) {
            std::cerr << "[Logger] Failed to create log directory: " << str_log_dir << "\n";
            return false;
        }
    }

    // Create log file with timestamp
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << str_log_dir << "/rest_daemon_"
       << std::put_time(std::localtime(&now_time_t), "%Y%m%d_%H%M%S")
       << ".log";
    m_str_log_file = ss.str();

    // Open log file in append mode
    m_log_stream.open(m_str_log_file, std::ios::out | std::ios::app);
    if (!m_log_stream.is_open()) {
        std::cerr << "[Logger] Failed to open log file: " << m_str_log_file << "\n";
        return false;
    }

    f_initialized = true;

    // Write initial log message
    Log(ELogLevel::TRACE, "Logger initialized, log file: " + m_str_log_file);

    return true;
}

/**
 * @brief Write log message with specified level
 * @param level Severity level of message
 * @param str_message Message text to log
 *
 * Thread-safe logging that:
 * - Filters messages below minimum level
 * - Writes timestamped message with process name and thread ID to file (OS-buffered)
 * - Also outputs ERROR/FATAL to stderr
 * Does nothing if logger not initialized.
 *
 * Log format: [timestamp] [level] [process:thread] message
 */
void CLogger::Log(ELogLevel level, const std::string& str_message) {
    if (!f_initialized) {
        return;
    }

    // Filter out messages below minimum log level
    if (static_cast<int>(level) < static_cast<int>(m_min_log_level)) {
        return;
    }

    std::lock_guard<std::recursive_mutex> lock(cs_log);

    std::string str_timestamp = GetTimestamp();
    std::string str_level = GetLevelString(level);
    std::string str_process = GetProcessName();
    std::string str_thread = GetThreadName();

    // Write to log file (OS-buffered, no immediate flush for performance)
    // Format: [timestamp] [level] [process:thread] message
    m_log_stream << "[" << str_timestamp << "] [" << str_level << "] ["
                 << str_process << ":" << str_thread << "] " << str_message << "\n";
    // note that flush() will not write to disk immediately but only to OS file descriptor.
    m_log_stream.flush();

    // Also write to console for errors and fatal
    if (level >= ELogLevel::ERROR) {
        std::cerr << "[" << str_timestamp << "] [" << str_level << "] ["
                  << str_process << ":" << str_thread << "] " << str_message << "\n";
    }
}

// Convenience wrappers for different log levels

void CLogger::Trace(const std::string& str_message) {
    Log(ELogLevel::TRACE, str_message);
}

void CLogger::Info(const std::string& str_message) {
    Log(ELogLevel::INFO, str_message);
}

void CLogger::Warn(const std::string& str_message) {
    Log(ELogLevel::WARN, str_message);
}

void CLogger::Error(const std::string& str_message) {
    Log(ELogLevel::ERROR, str_message);
}

void CLogger::Fatal(const std::string& str_message) {
    Log(ELogLevel::FATAL, str_message);
}

/**
 * @brief Change minimum log level threshold (thread-safe)
 * @param level New minimum level
 *
 * Messages below this level will be filtered out.
 */
void CLogger::SetMinLogLevel(ELogLevel level) {
    std::lock_guard<std::recursive_mutex> lock(cs_log);
    m_min_log_level = level;
}

/**
 * @brief Manually flush log buffer to disk (thread-safe)
 *
 * Forces immediate write of buffered log data. Normally not needed
 * as OS handles buffering, but useful before shutdown or for critical logs.
 */
void CLogger::Flush() {
    std::lock_guard<std::recursive_mutex> lock(cs_log);
    if (m_log_stream.is_open()) {
        m_log_stream.flush();
    }
}

// Global helper functions

/**
 * @brief Parse log level from string (wrapper function)
 * @param str_level String to parse
 * @return Corresponding ELogLevel
 */
ELogLevel ParseLogLevelString(const std::string& str_level) {
    return CLogger::ParseLogLevel(str_level);
}

/**
 * @brief Initialize global logger instance
 * @param str_log_dir Log directory path
 * @param min_level Minimum log level
 * @return true on success, false on failure
 *
 * Creates and initializes g_p_logger for use with LOG_* macros.
 */
bool InitializeLogger(const std::string& str_log_dir, ELogLevel min_level) {
    g_p_logger = std::make_shared<CLogger>();
    return g_p_logger->Initialize(str_log_dir, min_level);
}
