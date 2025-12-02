// ============= time_util.cpp =============
/**
 * @file time_util.cpp
 * @brief Implementation of mock time system for testing
 */

#include "time_util.h"

namespace TimeUtil {

// Global mock time state (0 = disabled, non-zero = mock time in seconds)
// Using atomic for thread-safe reads without mutex overhead
static std::atomic<int64_t> g_n_mock_time{0};

// Mutex protects write operations to ensure consistency
static std::mutex g_cs_mock_time;

int64_t GetCurrentTime() {
    // Check if mock time is enabled (lock-free atomic read)
    int64_t n_mock = g_n_mock_time.load(std::memory_order_relaxed);
    if (n_mock != 0) {
        return n_mock;
    }

    // Mock time disabled - return real system time
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

void SetMockTime(int64_t n_mock_time) {
    // Use mutex to ensure atomic write with proper memory ordering
    std::lock_guard<std::mutex> lock(g_cs_mock_time);
    g_n_mock_time.store(n_mock_time, std::memory_order_release);
}

int64_t GetMockTime() {
    return g_n_mock_time.load(std::memory_order_relaxed);
}

bool IsMockTimeEnabled() {
    return g_n_mock_time.load(std::memory_order_relaxed) != 0;
}

} // namespace TimeUtil
