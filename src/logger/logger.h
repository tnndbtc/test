// ============= logger.h =============
#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>
#include <memory>

enum class ELogLevel {
    INFO,
    ERROR
};

class CLogger {
private:
    std::string m_str_log_dir;
    std::string m_str_log_file;
    std::ofstream m_log_stream;
    std::recursive_mutex cs_log;  // Recursive to allow Initialize() to call Log()
    bool f_initialized;

    // Get current timestamp string
    static std::string GetTimestamp();

    // Get log level string
    static std::string GetLevelString(ELogLevel level);

public:
    CLogger();
    ~CLogger();

    // Initialize logger with log directory
    bool Initialize(const std::string& str_log_dir);

    // Log messages
    void Info(const std::string& str_message);
    void Error(const std::string& str_message);
    void Log(ELogLevel level, const std::string& str_message);

    // Check if logger is initialized
    bool IsInitialized() const { return f_initialized; }

    // Flush log to disk
    void Flush();
};

// Global logger instance
extern std::shared_ptr<CLogger> g_p_logger;

// Initialize global logger
bool InitializeLogger(const std::string& str_log_dir);

// Convenience macros for logging
#define LOG_INFO(msg) if (g_p_logger && g_p_logger->IsInitialized()) g_p_logger->Info(msg)
#define LOG_ERROR(msg) if (g_p_logger && g_p_logger->IsInitialized()) g_p_logger->Error(msg)

#endif
