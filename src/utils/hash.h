// ============= hash.h =============
#ifndef HASH_H
#define HASH_H

#include <string>

class CHash {
public:
    std::string m_str_data;

    CHash();
    explicit CHash(const std::string& input);

    bool operator==(const CHash& other) const;
    bool operator<(const CHash& other) const;
};

#endif
