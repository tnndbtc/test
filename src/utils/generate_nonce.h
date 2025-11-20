// ============= generate_nonce.h =============
#ifndef GENERATE_NONCE_H
#define GENERATE_NONCE_H
#include <random>
#include <cstdint>

inline uint64_t GenerateNonce() {
    static thread_local std::mt19937_64 gen(std::random_device{}());
    static thread_local std::uniform_int_distribution<uint64_t> dis;
    return dis(gen);
}

#endif // GENERATE_NONCE_H
