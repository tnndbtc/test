// ============= hash.h =============
#ifndef HASH_H
#define HASH_H

#include <string>

class CHash {
private:
    std::string m_str_data;

public:
    CHash();
    explicit CHash(const std::string& input);

    bool operator==(const CHash& other) const;
    bool operator<(const CHash& other) const;

    // Getter
    const std::string& GetData() const { return m_str_data; }
};

#endif
