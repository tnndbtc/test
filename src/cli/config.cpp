// ============= config.cpp =============
#include "config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

CConfig::CConfig() {
    LoadDefaults();
}

CConfig::CConfig(const std::string& str_config_path) {
    LoadDefaults();
    Load(str_config_path);
}

void CConfig::LoadDefaults() {
    m_config_values["miner_address"] = "";
    m_config_values["rest_api_port"] = std::to_string(REST_API_PORT);
    m_config_values["p2p_port"] = std::to_string(P2P_PORT);
    m_config_values["data_dir"] = "./data";
    m_config_values["log_dir"] = LOG_DIR;
    m_config_values["log_level"] = LOG_LEVEL;
    m_config_values["daemon"] = "false";
}

bool CConfig::Load(const std::string& str_config_path) {
    m_str_config_file = str_config_path;
    return ParseConfigFile(str_config_path);
}

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

        // Trim whitespace
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

std::string CConfig::GetValue(const std::string& str_key, const std::string& str_default) const {
    auto it = m_config_values.find(str_key);
    if (it != m_config_values.end()) {
        return it->second;
    }
    return str_default;
}

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

bool CConfig::GetBoolValue(const std::string& str_key, bool f_default) const {
    std::string str_value = GetValue(str_key);
    if (str_value.empty()) {
        return f_default;
    }
    std::transform(str_value.begin(), str_value.end(), str_value.begin(), ::tolower);
    return (str_value == "true" || str_value == "1" || str_value == "yes");
}

void CConfig::SetValue(const std::string& str_key, const std::string& str_value) {
    m_config_values[str_key] = str_value;
}

std::string CConfig::GetMinerAddress() const {
    return GetValue("miner_address");
}

int CConfig::GetRestApiPort() const {
    return GetIntValue("rest_api_port", REST_API_PORT);
}

int CConfig::GetP2PPort() const {
    return GetIntValue("p2p_port", P2P_PORT);
}

std::string CConfig::GetDataDir() const {
    return GetValue("data_dir", "./data");
}

std::string CConfig::GetLogDir() const {
    return GetValue("log_dir", LOG_DIR);
}

std::string CConfig::GetLogLevel() const {
    return GetValue("log_level", LOG_LEVEL);
}

bool CConfig::IsDaemonMode() const {
    return GetBoolValue("daemon", false);
}
