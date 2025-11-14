// ============= config.cpp =============
/**
 * @file config.cpp
 * @brief Implementation of configuration file parser
 *
 * Parses key=value configuration files and provides type-safe accessors.
 * Supports defaults from settings.h and runtime value modification.
 */

#include "config.h"
#include "pathutil.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <sys/stat.h>
#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <pwd.h>
#endif

/**
 * @brief Expand path by replacing ~ with home directory and handling relative paths
 * @param str_path Path to expand (may contain ~, be relative, or absolute)
 * @param str_parent_dir Parent directory for relative paths (default: ~/.bweave)
 * @return Expanded absolute path
 *
 * Path expansion rules:
 * 1. Absolute paths (/path or C:\path on Windows): returned as-is
 * 2. Paths starting with ~: expand to user's home directory
 * 3. Relative paths (data, log, etc.): expand to parent_dir/path
 *
 * Default parent directory is ~/.bweave on Linux/Mac, %APPDATA%\bweave on Windows
 */
static std::string ExpandPath(const std::string& str_path, const std::string& str_parent_dir = "") {
    if (str_path.empty()) {
        return str_path;
    }

    // Check if path is absolute
#ifdef _WIN32
    // Windows: Check for drive letter (C:) or UNC path (\\)
    if ((str_path.length() >= 2 && str_path[1] == ':') ||
        (str_path.length() >= 2 && str_path[0] == '\\' && str_path[1] == '\\')) {
        return str_path;  // Already absolute
    }
#else
    // POSIX: Check if path starts with /
    if (str_path[0] == '/') {
        return str_path;  // Already absolute
    }
#endif

    // Handle ~ expansion (home directory)
    if (str_path[0] == '~') {
        std::string str_home;
#ifdef _WIN32
        // Windows: Use USERPROFILE environment variable
        const char* p_userprofile = std::getenv("USERPROFILE");
        if (p_userprofile) {
            str_home = p_userprofile;
        }
#else
        // POSIX: Use HOME environment variable or getpwuid
        const char* p_home = std::getenv("HOME");
        if (p_home) {
            str_home = p_home;
        } else {
            struct passwd* pw = getpwuid(getuid());
            if (pw && pw->pw_dir) {
                str_home = pw->pw_dir;
            }
        }
#endif
        if (!str_home.empty()) {
            if (str_path.length() == 1) {
                // Just "~"
                return str_home;
            } else if (str_path[1] == '/' || str_path[1] == '\\') {
                // "~/path" or "~\path"
                return str_home + str_path.substr(1);
            }
        }
    }

    // Relative path: expand to parent_dir/path
    std::string str_effective_parent = str_parent_dir;
    if (str_effective_parent.empty()) {
        // Use default parent directory: ~/.bweave
#ifdef _WIN32
        const char* p_appdata = std::getenv("APPDATA");
        if (p_appdata) {
            str_effective_parent = std::string(p_appdata) + "\\bweave";
        } else {
            str_effective_parent = ".";  // Fallback to current directory
        }
#else
        const char* p_home = std::getenv("HOME");
        if (p_home) {
            str_effective_parent = std::string(p_home) + "/.bweave";
        } else {
            struct passwd* pw = getpwuid(getuid());
            if (pw && pw->pw_dir) {
                str_effective_parent = std::string(pw->pw_dir) + "/.bweave";
            } else {
                str_effective_parent = ".";  // Fallback to current directory
            }
        }
#endif
    }

#ifdef _WIN32
    return str_effective_parent + "\\" + str_path;
#else
    return str_effective_parent + "/" + str_path;
#endif
}

/**
 * @brief Default constructor - initialize with default values
 */
CConfig::CConfig() {
    LoadDefaults();
}

/**
 * @brief Constructor that loads configuration from file
 * @param str_config_path Path to configuration file
 *
 * Initializes with defaults then overlays values from file.
 */
CConfig::CConfig(const std::string& str_config_path) {
    LoadDefaults();
    Load(str_config_path);
}

/**
 * @brief Load default configuration values from settings.h
 *
 * Sets compile-time defaults for all configuration parameters.
 * These are used as fallback when values are not in config file.
 */
void CConfig::LoadDefaults() {
    m_config_values["miner_address"] = "";
    m_config_values["rest_api_port"] = std::to_string(REST_API_PORT);
    m_config_values["p2p_port"] = std::to_string(P2P_PORT);
    m_config_values["max_outbound_peers"] = std::to_string(MAX_OUTBOUND_PEERS);
    m_config_values["max_inbound_peers"] = std::to_string(MAX_INBOUND_PEERS);
    m_config_values["data_dir"] = "data";  // Relative path, will expand to ~/.bweave/data
    m_config_values["log_dir"] = "log";     // Relative path, will expand to ~/.bweave/log
    m_config_values["log_level"] = LOG_LEVEL;
    m_config_values["log_file_size_in_mb"] = std::to_string(LOG_FILE_SIZE_MB);
    m_config_values["log_file_keep"] = std::to_string(LOG_FILE_KEEP);
    m_config_values["peers_ping_time"] = std::to_string(PEERS_PING_TIME);
    m_config_values["daemon"] = "false";
    m_config_values["network"] = DEFAULT_NETWORK;
}

/**
 * @brief Load configuration from file
 * @param str_config_path Path to config file
 * @return true if file parsed successfully, false otherwise
 *
 * Stores config path and delegates parsing to ParseConfigFile().
 * Returns false if file doesn't exist but still uses defaults.
 */
bool CConfig::Load(const std::string& str_config_path) {
    m_str_config_file = str_config_path;
    return ParseConfigFile(str_config_path);
}

/**
 * @brief Parse configuration file line by line
 * @param str_file_path Path to config file
 * @return true if parsing succeeded, false on file error
 *
 * Parses key=value pairs from file, ignoring:
 * - Empty lines
 * - Lines starting with '#' (comments)
 * - Leading/trailing whitespace
 *
 * Invalid lines generate warnings but don't stop parsing.
 * Missing file generates warning and uses defaults.
 */
bool CConfig::ParseConfigFile(const std::string& str_file_path) {
    std::ifstream file(str_file_path);
    if (!file.is_open()) {
        std::cerr << "[Config] Warning: Could not open config file: " << str_file_path << "\n";
        std::cerr << "[Config] Using default values\n";
        return false;
    }

    std::string str_line;
    int n_line_number = 0;

    while (std::getline(file, str_line)) {
        n_line_number++;

        // Remove whitespace
        str_line.erase(0, str_line.find_first_not_of(" \t\r\n"));
        str_line.erase(str_line.find_last_not_of(" \t\r\n") + 1);

        // Skip empty lines and comments
        if (str_line.empty() || str_line[0] == '#') {
            continue;
        }

        // Parse key=value
        size_t n_pos = str_line.find('=');
        if (n_pos == std::string::npos) {
            std::cerr << "[Config] Warning: Invalid line " << n_line_number << ": " << str_line << "\n";
            continue;
        }

        std::string str_key = str_line.substr(0, n_pos);
        std::string str_value = str_line.substr(n_pos + 1);

        // Trim whitespace from key and value
        str_key.erase(0, str_key.find_first_not_of(" \t"));
        str_key.erase(str_key.find_last_not_of(" \t") + 1);
        str_value.erase(0, str_value.find_first_not_of(" \t"));
        str_value.erase(str_value.find_last_not_of(" \t") + 1);

        m_config_values[str_key] = str_value;
    }

    file.close();
    std::cout << "[Config] Loaded configuration from: " << str_file_path << "\n";
    return true;
}

/**
 * @brief Get configuration value as string
 * @param str_key Configuration key to look up
 * @param str_default Default value if key not found
 * @return Configuration value or default
 */
std::string CConfig::GetValue(const std::string& str_key, const std::string& str_default) const {
    auto it = m_config_values.find(str_key);
    if (it != m_config_values.end()) {
        return it->second;
    }
    return str_default;
}

/**
 * @brief Get configuration value as integer
 * @param str_key Configuration key to look up
 * @param n_default Default value if key not found or invalid
 * @return Configuration value converted to int, or default
 *
 * Returns default if value is empty or not a valid integer.
 */
int CConfig::GetIntValue(const std::string& str_key, int n_default) const {
    std::string str_value = GetValue(str_key);
    if (str_value.empty()) {
        return n_default;
    }
    try {
        return std::stoi(str_value);
    } catch (...) {
        return n_default;
    }
}

/**
 * @brief Get configuration value as boolean
 * @param str_key Configuration key to look up
 * @param f_default Default value if key not found
 * @return Configuration value converted to bool, or default
 *
 * Accepts (case-insensitive): "true", "yes", "1" as true
 * All other values are false.
 */
bool CConfig::GetBoolValue(const std::string& str_key, bool f_default) const {
    std::string str_value = GetValue(str_key);
    if (str_value.empty()) {
        return f_default;
    }
    std::transform(str_value.begin(), str_value.end(), str_value.begin(), ::tolower);
    return (str_value == "true" || str_value == "1" || str_value == "yes");
}

/**
 * @brief Set or update configuration value at runtime
 * @param str_key Configuration key
 * @param str_value New value
 *
 * Adds new key or updates existing value in memory only.
 * Does not modify the configuration file on disk.
 */
void CConfig::SetValue(const std::string& str_key, const std::string& str_value) {
    m_config_values[str_key] = str_value;
}

// Specialized getters for common configuration parameters

std::string CConfig::GetMinerAddress() const {
    return GetValue("miner_address");
}

int CConfig::GetRestApiPort() const {
    return GetIntValue("rest_api_port", REST_API_PORT);
}

int CConfig::GetP2PPort() const {
    return GetIntValue("p2p_port", P2P_PORT);
}

int CConfig::GetMaxOutboundPeers() const {
    return GetIntValue("max_outbound_peers", MAX_OUTBOUND_PEERS);
}

int CConfig::GetMaxInboundPeers() const {
    return GetIntValue("max_inbound_peers", MAX_INBOUND_PEERS);
}

std::string CConfig::GetDataDir() const {
    std::string str_path = GetValue("data_dir", "data");
    std::string str_expanded = ExpandPath(str_path);
    // Create directory silently if it doesn't exist
    CreateDirectoryRecursive(str_expanded);
    return str_expanded;
}

std::string CConfig::GetLogDir() const {
    std::string str_path = GetValue("log_dir", "log");
    std::string str_expanded = ExpandPath(str_path);
    // Create directory silently if it doesn't exist
    CreateDirectoryRecursive(str_expanded);
    return str_expanded;
}

std::string CConfig::GetLogLevel() const {
    return GetValue("log_level", LOG_LEVEL);
}

int CConfig::GetLogFileSizeMB() const {
    return GetIntValue("log_file_size_in_mb", LOG_FILE_SIZE_MB);
}

int CConfig::GetLogFileKeep() const {
    return GetIntValue("log_file_keep", LOG_FILE_KEEP);
}

int CConfig::GetPeersPingTime() const {
    return GetIntValue("peers_ping_time", PEERS_PING_TIME);
}

bool CConfig::IsDaemonMode() const {
    return GetBoolValue("daemon", false);
}

std::string CConfig::GetNetwork() const {
    return GetValue("network", DEFAULT_NETWORK);
}

std::string CConfig::GetNetworkDataDir(const std::string& str_network) const {
    std::string str_base_dir = GetDataDir();
    // Remove trailing slash if present
    if (!str_base_dir.empty() && str_base_dir.back() == '/') {
        str_base_dir.pop_back();
    }
    return str_base_dir + "/" + str_network;
}
