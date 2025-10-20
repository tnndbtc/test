// ============= threadname.h =============
/**
 * @file threadname.h
 * @brief Thread naming utilities for debugging and logging
 *
 * Provides cross-platform thread naming functionality using pthread.
 * Thread names are visible in debuggers and logging output, making
 * it easier to identify which thread is performing which operations.
 */

#ifndef THREADNAME_H
#define THREADNAME_H

#include <string>

/**
 * @brief Set the name of the current thread
 * @param str_name Thread name (max 15 characters on most platforms)
 *
 * Platform-specific implementation:
 * - macOS: Uses pthread_setname_np(name)
 * - Linux: Uses pthread_setname_np(pthread_self(), name)
 * - Other: No-op (silently ignored)
 *
 * Note: Thread names are typically limited to 15 characters on most
 * systems. Names longer than this will be automatically truncated.
 *
 * Example usage:
 *   SetThreadName("worker_0");
 *   SetThreadName("rest_listener");
 */
void SetThreadName(const std::string& str_name);

/**
 * @brief Get the name of the current thread
 * @return Thread name as string, or hex thread ID if name not set
 *
 * Platform-specific implementation:
 * - macOS: Uses pthread_getname_np()
 * - Linux: Uses pthread_getname_np(pthread_self(), ...)
 * - Other: Returns hex thread ID
 *
 * Falls back to hex thread ID if name is not set or retrieval fails.
 *
 * Example usage:
 *   std::string name = GetThreadName();  // Returns "worker_0" or "0x7f8a3c000"
 */
std::string GetThreadName();

#endif
