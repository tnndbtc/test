// ============= pathutil.h =============
/**
 * @file pathutil.h
 * @brief Path manipulation utilities
 *
 * Provides cross-platform utilities for path manipulation including
 * recursive directory creation (mkdir -p equivalent).
 */

#ifndef PATHUTIL_H
#define PATHUTIL_H

#include <string>

/**
 * @brief Create directory recursively (mkdir -p functionality)
 * @param str_path Directory path to create
 * @return true if directory exists or was created successfully, false on error
 *
 * Creates all parent directories as needed. If the directory already exists,
 * returns true silently. This implements the equivalent of "mkdir -p".
 *
 * Thread-safe: Multiple calls to CreateDirectoryRecursive with the same path
 * are safe (idempotent operation).
 *
 * @example
 * CreateDirectoryRecursive("/home/user/.bweave/data/mainnet/blocks");
 * // Creates: /home/user/.bweave, then data, then mainnet, then blocks
 */
bool CreateDirectoryRecursive(const std::string& str_path);

#endif // PATHUTIL_H
