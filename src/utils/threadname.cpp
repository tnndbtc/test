// ============= threadname.cpp =============
/**
 * @file threadname.cpp
 * @brief Implementation of thread naming utilities
 *
 * Provides cross-platform thread naming for debugging and logging.
 * Uses pthread API for setting and retrieving thread names.
 */

#include "threadname.h"
#include <pthread.h>
#include <thread>
#include <sstream>
#include <iomanip>

/**
 * @brief Set the name of the current thread
 * @param str_name Thread name to set
 *
 * Sets the name of the calling thread using platform-specific pthread API.
 * Thread names are limited to 15 characters on most systems and will be
 * automatically truncated if longer.
 *
 * Platform notes:
 * - macOS: pthread_setname_np(name) - sets name for current thread
 * - Linux: pthread_setname_np(pthread_self(), name) - requires thread handle
 * - Other platforms: No-op (function does nothing but doesn't error)
 */
void SetThreadName(const std::string& str_name) {
    // Truncate to 15 chars max (pthread limitation on most systems)
    std::string str_truncated = str_name;
    if (str_truncated.length() > 15) {
        str_truncated = str_truncated.substr(0, 15);
    }

#ifdef __APPLE__
    pthread_setname_np(str_truncated.c_str());
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), str_truncated.c_str());
#endif
    // No-op on other platforms
}

/**
 * @brief Get the name of the current thread
 * @return Thread name as string, or hex thread ID if name not set
 *
 * Retrieves the thread name set via SetThreadName() or pthread_setname_np().
 * Falls back to hex thread ID if name is not set or retrieval fails.
 *
 * Platform notes:
 * - macOS: pthread_getname_np(pthread_self(), buffer, size)
 * - Linux: pthread_getname_np(pthread_self(), buffer, size)
 * - Other platforms: Returns hex thread ID
 */
std::string GetThreadName() {
#if defined(__APPLE__) || defined(__linux__)
    char thread_name[256];
    if (pthread_getname_np(pthread_self(), thread_name, sizeof(thread_name)) == 0 && thread_name[0] != '\0') {
        return std::string(thread_name);
    }
#endif

    // Fallback to thread ID if name not available
    std::stringstream ss;
    ss << "0x" << std::hex << std::this_thread::get_id();
    return ss.str();
}
