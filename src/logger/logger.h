// ============= logger.h =============
#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>
#include <memory>

/**
 * @enum ELogLevel
 * @brief Log severity levels in ascending order of severity
 *
 * Defines the severity levels for logging. Lower numeric values indicate
 * more verbose output. Used for filtering log messages based on minimum level.
 */
enum class ELogLevel {
    TRACE = 0,  // Most verbose - detailed trace information
    INFO = 1,   // Informational messages
    WARN = 2,   // Warning messages
    ERROR = 3,  // Error messages
    FATAL = 4   // Least verbose - fatal errors only
};

/**
 * @class CLogger
 * @brief Thread-safe logging system for writing timestamped log messages to file
 *
 * Provides a centralized logging facility that writes timestamped, leveled
 * log messages to a file. Features include:
 * - Thread-safe logging using recursive mutex
 * - Configurable minimum log level for filtering
 * - Automatic timestamp generation with millisecond precision
 * - Console output for ERROR and FATAL levels
 * - OS-buffered writes (no immediate flush for performance)
 *
 * Example usage:
 *   CLogger logger;
 *   logger.Initialize("./logs", ELogLevel::INFO);
 *   logger.Info("Application started");
 *   logger.Error("Something went wrong");
 */
class CLogger {
private:
    std::string m_str_log_dir;      ///< Directory where log files are stored
    std::string m_str_log_file;     ///< Full path to current log file
    std::ofstream m_log_stream;     ///< Output file stream for writing logs
    std::recursive_mutex cs_log;    ///< Recursive mutex for thread safety (allows Initialize() to call Log())
    bool f_initialized;             ///< Flag indicating whether logger has been initialized
    ELogLevel m_min_log_level;      ///< Minimum log level threshold (filters out lower priority messages)

    /**
     * @brief Get current timestamp as formatted string
     * @return Timestamp string in format "YYYY-MM-DD HH:MM:SS.mmm"
     */
    static std::string GetTimestamp();

    /**
     * @brief Convert log level enum to string representation
     * @param level Log level to convert
     * @return String representation of log level (e.g., "INFO ", "ERROR")
     */
    static std::string GetLevelString(ELogLevel level);

public:
    /**
     * @brief Parse log level from string (case-insensitive)
     * @param str_level String representation of log level (e.g., "info", "ERROR", "trace")
     * @return Corresponding ELogLevel enum value, defaults to INFO if unknown
     */
    static ELogLevel ParseLogLevel(const std::string& str_level);

    /**
     * @brief Constructor - creates uninitialized logger
     */
    CLogger();

    /**
     * @brief Destructor - flushes and closes log file if open
     */
    ~CLogger();

    /**
     * @brief Initialize logger with log directory and minimum level
     * @param str_log_dir Directory path for log files (will be created if doesn't exist)
     * @param min_level Minimum log level to write (default: INFO)
     * @return true if initialization successful, false on error
     *
     * Creates timestamped log file in format: rest_daemon_YYYYMMDD_HHMMSS.log
     */
    bool Initialize(const std::string& str_log_dir, ELogLevel min_level = ELogLevel::INFO);

    /**
     * @brief Log a trace-level message
     * @param str_message Message to log
     */
    void Trace(const std::string& str_message);

    /**
     * @brief Log an info-level message
     * @param str_message Message to log
     */
    void Info(const std::string& str_message);

    /**
     * @brief Log a warning-level message
     * @param str_message Message to log
     */
    void Warn(const std::string& str_message);

    /**
     * @brief Log an error-level message (also outputs to stderr)
     * @param str_message Message to log
     */
    void Error(const std::string& str_message);

    /**
     * @brief Log a fatal-level message (also outputs to stderr)
     * @param str_message Message to log
     */
    void Fatal(const std::string& str_message);

    /**
     * @brief Log a message with specified level
     * @param level Severity level of the message
     * @param str_message Message to log
     *
     * Filters messages below minimum log level. ERROR and FATAL messages
     * are also written to stderr. Writes are OS-buffered for performance.
     */
    void Log(ELogLevel level, const std::string& str_message);

    /**
     * @brief Set minimum log level threshold
     * @param level New minimum log level
     *
     * Messages with severity below this level will be filtered out.
     */
    void SetMinLogLevel(ELogLevel level);

    /**
     * @brief Check if logger has been initialized
     * @return true if initialized, false otherwise
     */
    bool IsInitialized() const { return f_initialized; }

    /**
     * @brief Manually flush log buffer to disk
     *
     * Normally not needed as OS handles buffering, but can be called
     * to ensure logs are written immediately (e.g., before shutdown).
     */
    void Flush();
};

/// Global shared pointer to logger instance (initialized by InitializeLogger)
extern std::shared_ptr<CLogger> g_p_logger;

/**
 * @brief Parse log level from string (wrapper for CLogger::ParseLogLevel)
 * @param str_level String representation of log level
 * @return Corresponding ELogLevel enum value
 */
ELogLevel ParseLogLevelString(const std::string& str_level);

/**
 * @brief Initialize global logger instance
 * @param str_log_dir Directory path for log files
 * @param min_level Minimum log level to write (default: INFO)
 * @return true if initialization successful, false on error
 *
 * Creates and initializes the global g_p_logger instance.
 * Must be called before using LOG_* macros.
 */
bool InitializeLogger(const std::string& str_log_dir, ELogLevel min_level = ELogLevel::INFO);

/**
 * @name Logging Macros
 * @brief Convenience macros for logging using global logger instance
 *
 * These macros check if the global logger is initialized before logging.
 * Safe to use even if InitializeLogger() has not been called.
 * @{
 */
#define LOG_TRACE(msg) if (g_p_logger && g_p_logger->IsInitialized()) g_p_logger->Trace(msg)
#define LOG_INFO(msg) if (g_p_logger && g_p_logger->IsInitialized()) g_p_logger->Info(msg)
#define LOG_WARN(msg) if (g_p_logger && g_p_logger->IsInitialized()) g_p_logger->Warn(msg)
#define LOG_ERROR(msg) if (g_p_logger && g_p_logger->IsInitialized()) g_p_logger->Error(msg)
#define LOG_FATAL(msg) if (g_p_logger && g_p_logger->IsInitialized()) g_p_logger->Fatal(msg)
/** @} */

#endif
