// ============= address.cpp =============
/**
 * @file address.cpp
 * @brief Implementation of address utility functions
 */

#include "address.h"
#include "hash.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>

bool IsValidAddress(const std::vector<uint8_t>& address) {
    return address.size() == ADDRESS_LENGTH;
}

std::string AddressToHex(const std::vector<uint8_t>& address) {
    if (!IsValidAddress(address)) {
        return "";
    }

    // Use BytesToHex from hash.h and add "0x" prefix
    return "0x" + BytesToHex(address);
}

std::vector<uint8_t> HexToAddress(const std::string& str_hex) {
    // Use HexToBytes from hash.h
    return HexToBytes(str_hex);
}
