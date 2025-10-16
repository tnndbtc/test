// ============= config.h =============
#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <map>
#include "utils/settings.h"

class CConfig {
private:
    std::map<std::string, std::string> m_config_values;
    std::string m_str_config_file;

    void LoadDefaults();
    bool ParseConfigFile(const std::string& str_file_path);

public:
    CConfig();
    explicit CConfig(const std::string& str_config_path);

    bool Load(const std::string& str_config_path);
    std::string GetValue(const std::string& str_key, const std::string& str_default = "") const;
    int GetIntValue(const std::string& str_key, int n_default = 0) const;
    bool GetBoolValue(const std::string& str_key, bool f_default = false) const;
    void SetValue(const std::string& str_key, const std::string& str_value);

    // Specific configuration getters
    std::string GetMinerAddress() const;
    int GetRestApiPort() const;
    std::string GetDataDir() const;
    std::string GetLogDir() const;
    std::string GetLogLevel() const;
    bool IsDaemonMode() const;
};

#endif
