// ============= time_util.h =============
/**
 * @file time_util.h
 * @brief Mock time system for testing time-dependent logic
 *
 * Provides thread-safe mock time functionality for testing. When mock time
 * is enabled (non-zero), GetCurrentTime() returns the mock value instead of
 * real system time.
 *
 * Mock time can only be set via /rpc/setmocktime endpoint on localnet.
 * Attempting to set mock time on mainnet or testnet returns an error.
 */

#ifndef TIME_UTIL_H
#define TIME_UTIL_H

#include <cstdint>
#include <chrono>
#include <atomic>
#include <mutex>

/**
 * @namespace TimeUtil
 * @brief Time utility functions with mock time support for testing
 *
 * All time-dependent code should use GetCurrentTime() instead of directly
 * calling std::chrono::system_clock::now(). This allows tests to control
 * time progression via SetMockTime().
 */
namespace TimeUtil {

/**
 * @brief Get current timestamp in seconds
 * @return UNIX timestamp in seconds
 *
 * Returns mock time if set (non-zero), otherwise returns real system time.
 * Thread-safe: Uses atomic read for mock time value.
 *
 * Example:
 *   int64_t now = TimeUtil::GetCurrentTime();
 *   int64_t expiry = now + 86400;  // 24 hours from now
 */
int64_t GetCurrentTime();

/**
 * @brief Set mock time for testing (localnet only)
 * @param n_mock_time UNIX timestamp in seconds, or 0 to disable mock time
 *
 * When mock time is set to non-zero value, GetCurrentTime() returns this
 * value instead of real system time. Set to 0 to disable and return to
 * real time.
 *
 * Thread-safe: Uses mutex to protect write operation.
 *
 * Security: Should only be callable via /rpc/setmocktime on localnet.
 * The RPC handler enforces network restriction.
 *
 * Example:
 *   TimeUtil::SetMockTime(1234567890);  // Set time to specific value
 *   // ... run tests ...
 *   TimeUtil::SetMockTime(0);           // Disable mock time
 */
void SetMockTime(int64_t n_mock_time);

/**
 * @brief Get current mock time value
 * @return Current mock time in seconds, or 0 if mock time is disabled
 *
 * Thread-safe: Uses atomic read.
 *
 * Example:
 *   int64_t mock = TimeUtil::GetMockTime();
 *   if (mock != 0) {
 *       std::cout << "Mock time active: " << mock << std::endl;
 *   }
 */
int64_t GetMockTime();

/**
 * @brief Check if mock time is enabled
 * @return true if mock time is set (non-zero), false otherwise
 *
 * Thread-safe: Uses atomic read.
 *
 * Example:
 *   if (TimeUtil::IsMockTimeEnabled()) {
 *       LOG_WARN("Running with mock time - not production!");
 *   }
 */
bool IsMockTimeEnabled();

} // namespace TimeUtil

#endif // TIME_UTIL_H
