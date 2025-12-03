// ============= pathutil.cpp =============
/**
 * @file pathutil.cpp
 * @brief Implementation of path manipulation utilities
 *
 * Provides cross-platform recursive directory creation.
 */

#include "pathutil.h"
#include <sys/stat.h>

#ifdef _WIN32
    #include <direct.h>
    #define mkdir(path, mode) _mkdir(path)
    // Windows doesn't have S_ISDIR, use _S_IFDIR instead
    #ifndef S_ISDIR
        #define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
    #endif
#else
    #include <sys/types.h>
#endif

/**
 * @brief Create directory recursively (mkdir -p functionality)
 * @param str_path Directory path to create
 * @return true if directory exists or was created successfully, false on error
 *
 * Creates all parent directories as needed. If the directory already exists,
 * returns true silently. This implements the equivalent of "mkdir -p".
 */
bool CreateDirectoryRecursive(const std::string& str_path) {
    if (str_path.empty()) {
        return false;
    }

    // Check if directory already exists
    struct stat st;
    if (stat(str_path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true;  // Directory already exists
        } else {
            return false;  // Path exists but is not a directory
        }
    }

    // Find parent directory
    size_t n_pos = str_path.find_last_of("/\\");
    if (n_pos != std::string::npos) {
        std::string str_parent = str_path.substr(0, n_pos);
        if (!str_parent.empty() && str_parent != "/" && str_parent.length() != 2) {
            // Recursively create parent directory
            if (!CreateDirectoryRecursive(str_parent)) {
                return false;
            }
        }
    }

    // Create this directory
    int n_result = mkdir(str_path.c_str(), 0755);
    return (n_result == 0);
}
