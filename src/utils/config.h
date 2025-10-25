// ============= config.h =============
#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <map>
#include "utils/settings.h"

/**
 * @class CConfig
 * @brief Configuration file parser and manager
 *
 * Parses key-value configuration files (blockweave.conf) and provides
 * type-safe accessors for configuration values. Supports default values
 * from settings.h when configuration keys are not specified.
 *
 * Configuration file format:
 *   key=value
 *   # Comments start with hash
 *
 * Features:
 * - Key-value storage with string, int, and bool accessors
 * - Default value fallback from settings.h
 * - Specialized getters for common configuration parameters
 * - Runtime value modification via SetValue()
 *
 * Example usage:
 *   CConfig config("blockweave.conf");
 *   int port = config.GetRestApiPort();  // Returns configured or default port
 *   std::string miner = config.GetMinerAddress();
 */
class CConfig {
private:
    std::map<std::string, std::string> m_config_values;  ///< Parsed key-value pairs from config file
    std::string m_str_config_file;                       ///< Path to loaded configuration file

    /**
     * @brief Load default values from settings.h
     *
     * Populates config with compile-time defaults for optional settings.
     * Called during initialization before parsing config file.
     */
    void LoadDefaults();

    /**
     * @brief Parse configuration file
     * @param str_file_path Path to config file
     * @return true if file parsed successfully, false on error
     *
     * Reads file line by line, parsing key=value pairs and ignoring comments.
     */
    bool ParseConfigFile(const std::string& str_file_path);

public:
    /**
     * @brief Default constructor - creates empty config with defaults
     */
    CConfig();

    /**
     * @brief Construct and load config from file
     * @param str_config_path Path to configuration file
     */
    explicit CConfig(const std::string& str_config_path);

    /**
     * @brief Load configuration from file
     * @param str_config_path Path to configuration file
     * @return true if loaded successfully, false on error
     *
     * Loads defaults first, then overlays values from config file.
     */
    bool Load(const std::string& str_config_path);

    /**
     * @brief Get configuration value as string
     * @param str_key Configuration key
     * @param str_default Default value if key not found
     * @return Configuration value or default
     */
    std::string GetValue(const std::string& str_key, const std::string& str_default = "") const;

    /**
     * @brief Get configuration value as integer
     * @param str_key Configuration key
     * @param n_default Default value if key not found or invalid
     * @return Configuration value as int or default
     */
    int GetIntValue(const std::string& str_key, int n_default = 0) const;

    /**
     * @brief Get configuration value as boolean
     * @param str_key Configuration key
     * @param f_default Default value if key not found
     * @return Configuration value as bool or default
     *
     * Accepts: "true"/"false", "yes"/"no", "1"/"0" (case-insensitive)
     */
    bool GetBoolValue(const std::string& str_key, bool f_default = false) const;

    /**
     * @brief Set configuration value at runtime
     * @param str_key Configuration key
     * @param str_value New value
     *
     * Updates or adds key-value pair. Does not modify config file.
     */
    void SetValue(const std::string& str_key, const std::string& str_value);

    // Specialized configuration getters

    /**
     * @brief Get miner address for mining rewards
     * @return Miner address string (required configuration)
     */
    std::string GetMinerAddress() const;

    /**
     * @brief Get REST API server port
     * @return Port number (default: REST_API_PORT from settings.h)
     */
    int GetRestApiPort() const;

    /**
     * @brief Get P2P network port
     * @return Port number (default: P2P_PORT from settings.h if defined)
     */
    int GetP2PPort() const;

    /**
     * @brief Get maximum number of outbound peer connections
     * @return Max peers number (default: MAX_OUTBOUND_PEERS from settings.h)
     */
    int GetMaxOutboundPeers() const;

    /**
     * @brief Get maximum number of inbound peer connections
     * @return Max inbound peers number (default: MAX_INBOUND_PEERS from settings.h)
     */
    int GetMaxInboundPeers() const;

    /**
     * @brief Get data directory path
     * @return Directory path for blockchain data
     */
    std::string GetDataDir() const;

    /**
     * @brief Get log directory path
     * @return Directory path for log files (default: LOG_DIR from settings.h)
     */
    std::string GetLogDir() const;

    /**
     * @brief Get log level setting
     * @return Log level string (default: LOG_LEVEL from settings.h)
     */
    std::string GetLogLevel() const;

    /**
     * @brief Get log file size limit in megabytes
     * @return Maximum log file size in MB (default: LOG_FILE_SIZE_MB from settings.h)
     */
    int GetLogFileSizeMB() const;

    /**
     * @brief Get number of rotated log files to keep
     * @return Number of old log files to retain (default: LOG_FILE_KEEP from settings.h)
     */
    int GetLogFileKeep() const;

    /**
     * @brief Check if daemon mode is enabled
     * @return true if daemon=true in config, false otherwise
     */
    bool IsDaemonMode() const;
};

#endif
