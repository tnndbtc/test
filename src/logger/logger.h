// ============= logger.h =============
#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>
#include <memory>

enum class ELogLevel {
    TRACE = 0,  // Most verbose
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    FATAL = 4   // Least verbose
};

class CLogger {
private:
    std::string m_str_log_dir;
    std::string m_str_log_file;
    std::ofstream m_log_stream;
    std::recursive_mutex cs_log;  // Recursive to allow Initialize() to call Log()
    bool f_initialized;
    ELogLevel m_min_log_level;  // Minimum level to log (filters out lower priority)

    // Get current timestamp string
    static std::string GetTimestamp();

    // Get log level string
    static std::string GetLevelString(ELogLevel level);

public:
    // Parse log level from string
    static ELogLevel ParseLogLevel(const std::string& str_level);
    CLogger();
    ~CLogger();

    // Initialize logger with log directory and optional log level
    bool Initialize(const std::string& str_log_dir, ELogLevel min_level = ELogLevel::INFO);

    // Log messages
    void Trace(const std::string& str_message);
    void Info(const std::string& str_message);
    void Warn(const std::string& str_message);
    void Error(const std::string& str_message);
    void Fatal(const std::string& str_message);
    void Log(ELogLevel level, const std::string& str_message);

    // Set minimum log level
    void SetMinLogLevel(ELogLevel level);

    // Check if logger is initialized
    bool IsInitialized() const { return f_initialized; }

    // Flush log to disk
    void Flush();
};

// Global logger instance
extern std::shared_ptr<CLogger> g_p_logger;

// Parse log level from string
ELogLevel ParseLogLevelString(const std::string& str_level);

// Initialize global logger with log directory and optional log level
bool InitializeLogger(const std::string& str_log_dir, ELogLevel min_level = ELogLevel::INFO);

// Convenience macros for logging
#define LOG_TRACE(msg) if (g_p_logger && g_p_logger->IsInitialized()) g_p_logger->Trace(msg)
#define LOG_INFO(msg) if (g_p_logger && g_p_logger->IsInitialized()) g_p_logger->Info(msg)
#define LOG_WARN(msg) if (g_p_logger && g_p_logger->IsInitialized()) g_p_logger->Warn(msg)
#define LOG_ERROR(msg) if (g_p_logger && g_p_logger->IsInitialized()) g_p_logger->Error(msg)
#define LOG_FATAL(msg) if (g_p_logger && g_p_logger->IsInitialized()) g_p_logger->Fatal(msg)

#endif
