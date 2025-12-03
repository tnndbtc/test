// ============= threadname.cpp =============
/**
 * @file threadname.cpp
 * @brief Implementation of thread naming utilities
 *
 * Provides cross-platform thread naming for debugging and logging.
 * Uses pthread API for setting and retrieving thread names.
 */

#include "threadname.h"
#include <thread>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
    #include <windows.h>
    #include <processthreadsapi.h>
#else
    #include <pthread.h>
#endif

/**
 * @brief Set the name of the current thread
 * @param str_name Thread name to set
 *
 * Sets the name of the calling thread using platform-specific API.
 * Thread names are limited to 15 characters on pthread systems and will be
 * automatically truncated if longer.
 *
 * Platform notes:
 * - Windows: SetThreadDescription() - available on Windows 10 1607+
 * - macOS: pthread_setname_np(name) - sets name for current thread
 * - Linux: pthread_setname_np(pthread_self(), name) - requires thread handle
 */
void SetThreadName(const std::string& str_name) {
#ifdef _WIN32
    // Windows: Convert to wide string and use SetThreadDescription
    std::wstring wide_name(str_name.begin(), str_name.end());
    SetThreadDescription(GetCurrentThread(), wide_name.c_str());
#else
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
#endif
}

/**
 * @brief Get the name of the current thread
 * @return Thread name as string, or hex thread ID if name not set
 *
 * Retrieves the thread name set via SetThreadName().
 * Falls back to hex thread ID if name is not set or retrieval fails.
 *
 * Platform notes:
 * - Windows: GetThreadDescription() - available on Windows 10 1607+
 * - macOS: pthread_getname_np(pthread_self(), buffer, size)
 * - Linux: pthread_getname_np(pthread_self(), buffer, size)
 */
std::string GetThreadName() {
#ifdef _WIN32
    // Windows: Get thread description
    PWSTR thread_name = nullptr;
    HRESULT hr = GetThreadDescription(GetCurrentThread(), &thread_name);
    if (SUCCEEDED(hr) && thread_name != nullptr) {
        // Convert wide string to narrow string
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, thread_name, -1, NULL, 0, NULL, NULL);
        std::string result(size_needed - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, thread_name, -1, &result[0], size_needed, NULL, NULL);
        LocalFree(thread_name);
        if (!result.empty()) {
            return result;
        }
    }
#elif defined(__APPLE__) || defined(__linux__)
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
