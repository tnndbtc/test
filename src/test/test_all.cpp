// ============= test_all.cpp =============
/**
 * @file test_all.cpp
 * @brief Main entry point for all unit tests
 *
 * This file combines all unit test modules into a single executable.
 * Individual test files (test_peer.cpp, test_rest.cpp) contain the
 * actual test implementations and are compiled together with this file.
 *
 * Test modules included:
 * - test_peer.cpp - Peer networking module tests (12 tests)
 * - test_rest.cpp - REST API module tests (15 tests)
 *
 * Total: 27 unit tests
 *
 * Command-line options:
 * - --run_test=<filter>  : Run only tests matching filter (substring match)
 * - --list               : List all available tests
 * - --help               : Show usage information
 */

#include "unit_test.h"
#include <iostream>
#include <string>
#include <cstring>

/**
 * @brief Parse command-line argument for test filter
 * @param arg Command-line argument string
 * @param prefix Expected prefix (e.g., "--run_test=")
 * @return Filter value if argument matches prefix, empty string otherwise
 */
std::string ParseArgument(const char* arg, const char* prefix) {
    size_t prefix_len = std::strlen(prefix);
    if (std::strncmp(arg, prefix, prefix_len) == 0) {
        return std::string(arg + prefix_len);
    }
    return "";
}

/**
 * @brief Show usage information
 */
void ShowUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --run_test=<filter>   Run only tests matching filter (substring match)\n";
    std::cout << "  --list                List all available tests\n";
    std::cout << "  --help                Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << "                      # Run all tests\n";
    std::cout << "  " << program_name << " --run_test=Peer     # Run all peer tests\n";
    std::cout << "  " << program_name << " --run_test=Rest     # Run all REST API tests\n";
    std::cout << "  " << program_name << " --run_test=Queue    # Run all RequestQueue tests\n";
    std::cout << "  " << program_name << " --list              # List all tests\n\n";
    std::cout << "Test modules:\n";
    std::cout << "  - test_peer.cpp (Peer networking - 12 tests)\n";
    std::cout << "    Patterns: PeerConnection_, PeerManager_\n";
    std::cout << "  - test_rest.cpp (REST API - 15 tests)\n";
    std::cout << "    Patterns: HttpRequest_, RequestQueue_, RestApiServer_\n";
}

/**
 * @brief Main function - runs all registered unit tests
 * @param argc Number of command-line arguments
 * @param argv Array of command-line argument strings
 * @return 0 if all tests pass, 1 if any tests fail
 *
 * Tests are automatically registered via the TEST() macro in each
 * test file. The RunAllTests() function executes all registered tests
 * and displays results with color-coded output.
 *
 * Supports command-line filtering to run specific test subsets.
 */
int main(int argc, char* argv[]) {
    std::string filter;
    bool list_tests = false;
    bool show_help = false;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg_filter = ParseArgument(argv[i], "--run_test=");
        if (!arg_filter.empty()) {
            filter = arg_filter;
        } else if (std::strcmp(argv[i], "--list") == 0) {
            list_tests = true;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            show_help = true;
        } else {
            std::cerr << "Unknown option: " << argv[i] << "\n\n";
            ShowUsage(argv[0]);
            return 1;
        }
    }

    // Show help and exit
    if (show_help) {
        ShowUsage(argv[0]);
        return 0;
    }

    // List tests and exit
    if (list_tests) {
        std::cout << "======================================================================\n";
        std::cout << "Blockweave Unit Test Suite\n";
        std::cout << "======================================================================\n";
        std::cout << "Test modules:\n";
        std::cout << "  - test_peer.cpp (Peer networking - 12 tests)\n";
        std::cout << "  - test_rest.cpp (REST API - 15 tests)\n";
        std::cout << "======================================================================\n\n";

        UnitTest::ListAllTests();
        return 0;
    }

    // Run tests (with optional filter)
    std::cout << "======================================================================\n";
    std::cout << "Blockweave Unit Test Suite\n";
    std::cout << "======================================================================\n";
    std::cout << "Test modules:\n";
    std::cout << "  - test_peer.cpp (Peer networking - 12 tests)\n";
    std::cout << "  - test_rest.cpp (REST API - 15 tests)\n";
    std::cout << "======================================================================\n\n";

    return UnitTest::RunAllTests(filter);
}
