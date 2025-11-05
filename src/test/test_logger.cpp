// ============= test_logger.cpp =============
/**
 * @file test_logger.cpp
 * @brief Unit tests for CLogger class
 *
 * Tests the logging system including:
 * - Logger initialization and configuration
 * - Different log levels and filtering
 * - Log file creation and writing
 * - Log rotation based on file size
 * - Thread-safe concurrent logging
 * - Log format validation (timestamp, level, process, thread, message)
 */

#include "unit_test.h"
#include "../logger/logger.h"
#include <thread>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <sys/stat.h>
#include <filesystem>

#ifdef _WIN32
    #include <windows.h>
    // Windows defines ERROR as a macro, which conflicts with ELogLevel::ERROR
    #ifdef ERROR
        #undef ERROR
    #endif
#else
    #include <dirent.h>
#endif

// ============= Helper Functions =============

/**
 * @brief Generate a temporary directory name for test logs
 * @return Path to temporary directory (not created, logger.Initialize() will create it)
 */
static std::string CreateTempLogDirName() {
    std::string str_dir = "./test_logger_" + std::to_string(time(nullptr)) +
                          "_" + std::to_string(rand() % 10000);
    return str_dir;
}

/**
 * @brief Get the expected log file path for the current process
 * @param str_log_dir Log directory
 * @return Full path to log file (e.g., "./log/test_all.log")
 */
static std::string GetLogFilePath(const std::string& str_log_dir) {
    // Logger uses process name as log file name
    // For tests, the process is "test_all"
#ifdef _WIN32
    return str_log_dir + "\\test_all.log";
#else
    return str_log_dir + "/test_all.log";
#endif
}

/**
 * @brief Recursively remove directory and all contents
 * @param str_path Path to directory to remove
 */
static void CleanupLogDir(const std::string& str_path) {
    // Use C++17 filesystem for cross-platform directory removal
    std::error_code ec;
    std::filesystem::remove_all(str_path, ec);
    // Ignore errors - directory may not exist or already cleaned up
}

/**
 * @brief Check if file exists
 * @param str_path File path
 * @return true if file exists, false otherwise
 */
static bool FileExists(const std::string& str_path) {
    struct stat st;
    return stat(str_path.c_str(), &st) == 0;
}

/**
 * @brief Read entire file contents into string
 * @param str_path File path
 * @return File contents as string
 */
static std::string ReadFile(const std::string& str_path) {
    std::ifstream ifs(str_path);
    if (!ifs.is_open()) {
        return "";
    }
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

/**
 * @brief Count number of lines in file
 * @param str_path File path
 * @return Number of lines
 */
static size_t CountLines(const std::string& str_path) {
    std::ifstream ifs(str_path);
    if (!ifs.is_open()) {
        return 0;
    }
    size_t n_count = 0;
    std::string str_line;
    while (std::getline(ifs, str_line)) {
        n_count++;
    }
    return n_count;
}

/**
 * @brief Get file size in bytes
 * @param str_path File path
 * @return File size in bytes
 */
/*
static size_t GetFileSize(const std::string& str_path) {
    struct stat st;
    if (stat(str_path.c_str(), &st) == 0) {
        return static_cast<size_t>(st.st_size);
    }
    return 0;
}
*/
/**
 * @brief Count number of files matching pattern in directory
 * @param str_dir Directory path
 * @param str_pattern File name pattern (e.g., "test_all.log")
 * @return Number of matching files
 */
static size_t CountFilesWithPattern(const std::string& str_dir, const std::string& str_pattern) {
#ifdef _WIN32
    // Windows implementation using FindFirstFile/FindNextFile
    WIN32_FIND_DATAA find_data;
    std::string search_path = str_dir + "\\*";
    HANDLE h_find = FindFirstFileA(search_path.c_str(), &find_data);

    if (h_find == INVALID_HANDLE_VALUE) {
        return 0;
    }

    size_t n_count = 0;
    do {
        std::string str_name = find_data.cFileName;
        if (str_name.find(str_pattern) != std::string::npos) {
            n_count++;
        }
    } while (FindNextFileA(h_find, &find_data));
    FindClose(h_find);
    return n_count;
#else
    // POSIX implementation
    DIR* p_dir = opendir(str_dir.c_str());
    if (!p_dir) {
        return 0;
    }

    size_t n_count = 0;
    struct dirent* p_entry;
    while ((p_entry = readdir(p_dir)) != nullptr) {
        std::string str_name = p_entry->d_name;
        if (str_name.find(str_pattern) != std::string::npos) {
            n_count++;
        }
    }
    closedir(p_dir);
    return n_count;
#endif
}

// ============= Initialization Tests =============

TEST(LoggerConstructor) {
    CLogger logger;
    ASSERT_FALSE(logger.IsInitialized(), "Logger should not be initialized after construction");
}

TEST(LoggerInitializeCreatesDirectory) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        bool f_result = logger.Initialize(str_log_dir);

        ASSERT_TRUE(f_result, "Initialize should succeed");
        ASSERT_TRUE(logger.IsInitialized(), "Logger should be initialized");

        // Verify directory was created
        struct stat st;
        ASSERT_EQUAL(stat(str_log_dir.c_str(), &st), 0, "Log directory should exist");
        ASSERT_TRUE(S_ISDIR(st.st_mode), "Path should be a directory");

        // Verify log file was created
        std::string str_log_file = GetLogFilePath(str_log_dir);
        ASSERT_TRUE(FileExists(str_log_file), "Log file should be created");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

// ============= Log Level Tests =============

TEST(LoggerTraceMessage) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::TRACE);
        logger.Trace("Test trace message");
        logger.Flush();

        std::string str_log_file = GetLogFilePath(str_log_dir);
        std::string str_content = ReadFile(str_log_file);

        ASSERT_TRUE(str_content.find("Test trace message") != std::string::npos,
                    "Log should contain trace message");
        ASSERT_TRUE(str_content.find("[TRACE]") != std::string::npos,
                    "Log should contain TRACE level");
    }

    CleanupLogDir(str_log_dir);
}

TEST(LoggerInfoMessage) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::INFO);
        logger.Info("Test info message");
        logger.Flush();

        std::string str_log_file = GetLogFilePath(str_log_dir);
        std::string str_content = ReadFile(str_log_file);

        ASSERT_TRUE(str_content.find("Test info message") != std::string::npos,
                    "Log should contain info message");
        ASSERT_TRUE(str_content.find("[INFO ]") != std::string::npos,
                    "Log should contain INFO level");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

TEST(LoggerWarnMessage) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::WARN);
        logger.Warn("Test warning message");
        logger.Flush();

        std::string str_log_file = GetLogFilePath(str_log_dir);
        std::string str_content = ReadFile(str_log_file);

        ASSERT_TRUE(str_content.find("Test warning message") != std::string::npos,
                    "Log should contain warning message");
        ASSERT_TRUE(str_content.find("[WARN ]") != std::string::npos,
                    "Log should contain WARN level");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

TEST(LoggerErrorMessage) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::ERROR);
        logger.Error("Test error message");
        logger.Flush();

        std::string str_log_file = GetLogFilePath(str_log_dir);
        std::string str_content = ReadFile(str_log_file);

        ASSERT_TRUE(str_content.find("Test error message") != std::string::npos,
                    "Log should contain error message");
        ASSERT_TRUE(str_content.find("[ERROR]") != std::string::npos,
                    "Log should contain ERROR level");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

TEST(LoggerFatalMessage) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::FATAL);
        logger.Fatal("Test fatal message");
        logger.Flush();

        std::string str_log_file = GetLogFilePath(str_log_dir);
        std::string str_content = ReadFile(str_log_file);

        ASSERT_TRUE(str_content.find("Test fatal message") != std::string::npos,
                    "Log should contain fatal message");
        ASSERT_TRUE(str_content.find("[FATAL]") != std::string::npos,
                    "Log should contain FATAL level");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

TEST(LoggerGenericLogMethod) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::TRACE);
        logger.Log(ELogLevel::INFO, "Test generic log");
        logger.Flush();

        std::string str_log_file = GetLogFilePath(str_log_dir);
        std::string str_content = ReadFile(str_log_file);

        ASSERT_TRUE(str_content.find("Test generic log") != std::string::npos,
                    "Log should contain message");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

// ============= Log Level Filtering Tests =============

TEST(LoggerLevelFilteringTraceNotLogged) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::INFO); // Set min level to INFO
        logger.Trace("This should not be logged");
        logger.Info("This should be logged");
        logger.Flush();

        std::string str_log_file = GetLogFilePath(str_log_dir);
        std::string str_content = ReadFile(str_log_file);

        ASSERT_FALSE(str_content.find("This should not be logged") != std::string::npos,
                     "TRACE message should be filtered out");
        ASSERT_TRUE(str_content.find("This should be logged") != std::string::npos,
                    "INFO message should be logged");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

TEST(LoggerLevelFilteringInfoNotLogged) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::ERROR); // Set min level to ERROR
        logger.Info("Info should not appear");
        logger.Warn("Warning should not appear");
        logger.Error("Error should appear");
        logger.Flush();

        std::string str_log_file = GetLogFilePath(str_log_dir);
        std::string str_content = ReadFile(str_log_file);

        ASSERT_FALSE(str_content.find("Info should not appear") != std::string::npos,
                     "INFO message should be filtered out");
        ASSERT_FALSE(str_content.find("Warning should not appear") != std::string::npos,
                     "WARN message should be filtered out");
        ASSERT_TRUE(str_content.find("Error should appear") != std::string::npos,
                    "ERROR message should be logged");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

TEST(LoggerSetMinLogLevel) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::TRACE);

        // Initially, trace messages should be logged
        logger.Trace("Trace 1");
        logger.Flush();

        // Change min level to WARN
        logger.SetMinLogLevel(ELogLevel::WARN);
        logger.Trace("Trace 2");
        logger.Info("Info 2");
        logger.Warn("Warn 2");
        logger.Flush();

        std::string str_log_file = GetLogFilePath(str_log_dir);
        std::string str_content = ReadFile(str_log_file);

        ASSERT_TRUE(str_content.find("Trace 1") != std::string::npos,
                    "First trace message should be logged");
        ASSERT_FALSE(str_content.find("Trace 2") != std::string::npos,
                     "Second trace message should be filtered out");
        ASSERT_FALSE(str_content.find("Info 2") != std::string::npos,
                     "Info message should be filtered out");
        ASSERT_TRUE(str_content.find("Warn 2") != std::string::npos,
                    "Warn message should be logged");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

// ============= Log Format Tests =============

TEST(LoggerTimestampFormat) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::INFO);
        logger.Info("Test timestamp");
        logger.Flush();

        std::string str_log_file = GetLogFilePath(str_log_dir);
        std::string str_content = ReadFile(str_log_file);

        // Timestamp format: [YYYY-MM-DD HH:MM:SS.mmm UTC]
        // Look for pattern like [2025-10-25 00:12:34.567 UTC]
        ASSERT_TRUE(str_content.find("UTC]") != std::string::npos,
                    "Log should contain UTC timestamp");
        ASSERT_TRUE(str_content.find("[20") != std::string::npos,
                    "Log should contain year prefix");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

TEST(LoggerProcessAndThreadName) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::INFO);
        logger.Info("Test process and thread");
        logger.Flush();

        std::string str_log_file = GetLogFilePath(str_log_dir);
        std::string str_content = ReadFile(str_log_file);

        // Log format includes [process:thread]
        ASSERT_TRUE(str_content.find(":") != std::string::npos,
                    "Log should contain process:thread separator");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

// ============= Multiple Messages Tests =============

TEST(LoggerMultipleMessages) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::INFO);

        for (int i = 0; i < 10; i++) {
            logger.Info("Message " + std::to_string(i));
        }
        logger.Flush();

        std::string str_log_file = GetLogFilePath(str_log_dir);
        size_t n_lines = CountLines(str_log_file);

        // Should have at least 10 messages (plus initialization message)
        ASSERT_TRUE(n_lines >= 10, "Log should contain at least 10 lines");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

TEST(LoggerFlushWritesToDisk) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::INFO);
        logger.Info("Before flush");

        // Flush and verify file has content
        std::string str_log_file = GetLogFilePath(str_log_dir);
        logger.Flush();

        // Windows may not update file size via stat() while file is open for append
        // So we read the file content instead of checking size
        std::string str_content = ReadFile(str_log_file);

        ASSERT_TRUE(str_content.length() > 0, "File should have content after flush");
        ASSERT_TRUE(str_content.find("Before flush") != std::string::npos, "File should contain logged message");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

// ============= Log Rotation Tests =============

TEST(LoggerRotationFileNaming) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::INFO, 1, 3); // Keep 3 rotated files

        // Write enough to trigger multiple rotations
        for (int i = 0; i < 200; i++) {
            logger.Info("Message to trigger rotation: " + std::to_string(i) + " - Adding more text to increase size");
        }
        logger.Flush();

        // Count log files (test_all.log + rotated files)
        size_t n_log_files = CountFilesWithPattern(str_log_dir, "test_all.log");

        // Should have current log + up to 3 rotated files (1-4 files total)
        ASSERT_TRUE(n_log_files >= 1, "Should have at least the main log file");
        ASSERT_TRUE(n_log_files <= 4, "Should not exceed max rotated files + current");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

TEST(LoggerRotationDeletesOldFiles) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::INFO, 1, 2); // Keep only 2 rotated files

        // Write enough to trigger many rotations
        for (int i = 0; i < 300; i++) {
            logger.Info("Rotation test message: " + std::to_string(i) + " - Filling up log to test rotation limits");
        }
        logger.Flush();

        // Should not have more than current + 2 rotated files (3 total)
        size_t n_log_files = CountFilesWithPattern(str_log_dir, "test_all.log");
        ASSERT_TRUE(n_log_files <= 3, "Should delete old rotated files");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

// ============= Thread Safety Tests =============

TEST(LoggerThreadSafeConcurrentWrites) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::INFO);

        // Spawn multiple threads that log concurrently
        std::vector<std::thread> threads;
        for (int i = 0; i < 10; i++) {
            threads.emplace_back([&logger, i]() {
                for (int j = 0; j < 10; j++) {
                    logger.Info("Thread " + std::to_string(i) + " message " + std::to_string(j));
                }
            });
        }

        // Wait for all threads to finish
        for (auto& thread : threads) {
            thread.join();
        }

        logger.Flush();

        std::string str_log_file = GetLogFilePath(str_log_dir);
        size_t n_lines = CountLines(str_log_file);

        // Should have 10 threads * 10 messages = 100 messages (plus init message)
        ASSERT_TRUE(n_lines >= 100, "All concurrent messages should be logged");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

// ============= ParseLogLevel Tests =============

TEST(ParseLogLevelValidStrings) {
    ASSERT_EQUAL(static_cast<int>(CLogger::ParseLogLevel("TRACE")), static_cast<int>(ELogLevel::TRACE),
                 "Should parse TRACE");
    ASSERT_EQUAL(static_cast<int>(CLogger::ParseLogLevel("INFO")), static_cast<int>(ELogLevel::INFO),
                 "Should parse INFO");
    ASSERT_EQUAL(static_cast<int>(CLogger::ParseLogLevel("WARN")), static_cast<int>(ELogLevel::WARN),
                 "Should parse WARN");
    ASSERT_EQUAL(static_cast<int>(CLogger::ParseLogLevel("ERROR")), static_cast<int>(ELogLevel::ERROR),
                 "Should parse ERROR");
    ASSERT_EQUAL(static_cast<int>(CLogger::ParseLogLevel("FATAL")), static_cast<int>(ELogLevel::FATAL),
                 "Should parse FATAL");
}

TEST(ParseLogLevelCaseInsensitive) {
    ASSERT_EQUAL(static_cast<int>(CLogger::ParseLogLevel("trace")), static_cast<int>(ELogLevel::TRACE),
                 "Should parse lowercase trace");
    ASSERT_EQUAL(static_cast<int>(CLogger::ParseLogLevel("Info")), static_cast<int>(ELogLevel::INFO),
                 "Should parse mixed case Info");
    ASSERT_EQUAL(static_cast<int>(CLogger::ParseLogLevel("wArN")), static_cast<int>(ELogLevel::WARN),
                 "Should parse mixed case wArN");
}

TEST(ParseLogLevelWarningAlias) {
    ASSERT_EQUAL(static_cast<int>(CLogger::ParseLogLevel("WARNING")), static_cast<int>(ELogLevel::WARN),
                 "Should parse WARNING as WARN");
}

TEST(ParseLogLevelInvalidString) {
    ASSERT_EQUAL(static_cast<int>(CLogger::ParseLogLevel("INVALID")), static_cast<int>(ELogLevel::INFO),
                 "Invalid string should default to INFO");
    ASSERT_EQUAL(static_cast<int>(CLogger::ParseLogLevel("")), static_cast<int>(ELogLevel::INFO),
                 "Empty string should default to INFO");
}

// ============= Edge Cases =============

TEST(LoggerEmptyMessage) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::INFO);
        logger.Info(""); // Empty message
        logger.Flush();

        std::string str_log_file = GetLogFilePath(str_log_dir);
        ASSERT_TRUE(FileExists(str_log_file), "Log file should exist even with empty message");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

TEST(LoggerVeryLongMessage) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::INFO);

        // Create a very long message (10,000 characters)
        std::string str_long_msg(10000, 'X');
        logger.Info(str_long_msg);
        logger.Flush();

        std::string str_log_file = GetLogFilePath(str_log_dir);
        std::string str_content = ReadFile(str_log_file);

        ASSERT_TRUE(str_content.find(str_long_msg) != std::string::npos,
                    "Log should contain very long message");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

TEST(LoggerSpecialCharacters) {
    std::string str_log_dir = CreateTempLogDirName();

    {
        CLogger logger;
        logger.Initialize(str_log_dir, ELogLevel::INFO);
        logger.Info("Special chars: \n\t\"'\\");
        logger.Flush();

        std::string str_log_file = GetLogFilePath(str_log_dir);
        std::string str_content = ReadFile(str_log_file);

        ASSERT_TRUE(str_content.find("Special chars:") != std::string::npos,
                    "Log should handle special characters");
    } // Logger destroyed here, closes file

    CleanupLogDir(str_log_dir);
}

TEST(LoggerUninitializedDoesNotCrash) {
    CLogger logger;
    // Should not crash when logging to uninitialized logger
    logger.Info("This should not crash");
    logger.Trace("This should also not crash");
    logger.Flush(); // Should also not crash

    ASSERT_FALSE(logger.IsInitialized(), "Logger should remain uninitialized");
}
