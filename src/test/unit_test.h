// ============= unit_test.h =============
#ifndef UNIT_TEST_H
#define UNIT_TEST_H

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <sstream>

/**
 * @brief Simple unit test framework for C++ tests
 *
 * Provides basic test assertions and test runner functionality.
 * Similar to popular frameworks but lightweight and dependency-free.
 */

namespace UnitTest {

// Global test state
struct TestStats {
    int total = 0;
    int passed = 0;
    int failed = 0;
};

inline TestStats g_stats;

// Test failure exception
class TestFailure : public std::exception {
private:
    std::string m_message;
public:
    explicit TestFailure(const std::string& msg) : m_message(msg) {}
    const char* what() const noexcept override { return m_message.c_str(); }
};

// Assert macros
#define ASSERT_TRUE(condition, message) \
    do { \
        if (!(condition)) { \
            std::ostringstream oss; \
            oss << "ASSERTION FAILED: " << message << "\n" \
                << "  at " << __FILE__ << ":" << __LINE__ << "\n" \
                << "  Expression: " << #condition; \
            throw UnitTest::TestFailure(oss.str()); \
        } \
    } while(0)

#define ASSERT_FALSE(condition, message) \
    ASSERT_TRUE(!(condition), message)

#define ASSERT_EQUAL(actual, expected, message) \
    do { \
        if ((actual) != (expected)) { \
            std::ostringstream oss; \
            oss << "ASSERTION FAILED: " << message << "\n" \
                << "  at " << __FILE__ << ":" << __LINE__ << "\n" \
                << "  Expected: " << (expected) << "\n" \
                << "  Actual: " << (actual); \
            throw UnitTest::TestFailure(oss.str()); \
        } \
    } while(0)

#define ASSERT_NOT_EQUAL(actual, not_expected, message) \
    do { \
        if ((actual) == (not_expected)) { \
            std::ostringstream oss; \
            oss << "ASSERTION FAILED: " << message << "\n" \
                << "  at " << __FILE__ << ":" << __LINE__ << "\n" \
                << "  Should not equal: " << (not_expected) << "\n" \
                << "  Actual: " << (actual); \
            throw UnitTest::TestFailure(oss.str()); \
        } \
    } while(0)

#define ASSERT_NULL(ptr, message) \
    ASSERT_TRUE((ptr) == nullptr, message)

#define ASSERT_NOT_NULL(ptr, message) \
    ASSERT_TRUE((ptr) != nullptr, message)

// Test case structure
struct TestCase {
    std::string name;
    std::string file;
    std::function<void()> test_func;
};

// Test registry
inline std::vector<TestCase>& GetTests() {
    static std::vector<TestCase> tests;
    return tests;
}

// Register a test
inline void RegisterTest(const std::string& name, const std::string& file, std::function<void()> test_func) {
    GetTests().push_back({name, file, test_func});
}

// Convert string to lowercase
inline std::string ToLower(const std::string& str) {
    std::string result = str;
    for (char& c : result) {
        c = std::tolower(static_cast<unsigned char>(c));
    }
    return result;
}

// Check if test file matches filter
inline bool MatchesFilter(const std::string& test_file, const std::string& filter) {
    if (filter.empty()) {
        return true;  // No filter, match all
    }

    // Convert both to lowercase for case-insensitive matching
    std::string lower_file = ToLower(test_file);
    std::string lower_filter = ToLower(filter);

    // Remove .cpp extension from file for matching
    std::string file_base = lower_file;
    if (file_base.length() > 4 && file_base.substr(file_base.length() - 4) == ".cpp") {
        file_base = file_base.substr(0, file_base.length() - 4);
    }

    // Add .cpp to filter if not present
    std::string filter_with_ext = lower_filter;
    if (filter_with_ext.length() <= 4 || filter_with_ext.substr(filter_with_ext.length() - 4) != ".cpp") {
        filter_with_ext += ".cpp";
    }

    // Match: filter matches file with or without .cpp extension
    return (lower_file == lower_filter ||
            lower_file == filter_with_ext ||
            file_base == lower_filter);
}

// Run all registered tests, optionally filtered by name pattern
inline int RunAllTests(const std::string& filter = "") {
    std::cout << "======================================================================\n";
    std::cout << "Running Unit Tests";
    if (!filter.empty()) {
        std::cout << " (filter: \"" << filter << "\")";
    }
    std::cout << "\n";
    std::cout << "======================================================================\n\n";

    int skipped = 0;
    std::string current_file;

    for (const auto& test : GetTests()) {
        if (!MatchesFilter(test.file, filter)) {
            skipped++;
            continue;
        }

        // Print file header when entering a new test file
        if (test.file != current_file) {
            if (!current_file.empty()) {
                std::cout << "\n";  // Add spacing between files
            }
            current_file = test.file;
            std::cout << "----------------------------------------------------------------------\n";
            std::cout << "Test File: " << current_file << "\n";
            std::cout << "----------------------------------------------------------------------\n";
        }

        g_stats.total++;
        std::cout << "Running: " << test.name << " ... ";

        try {
            test.test_func();
            g_stats.passed++;
            std::cout << " PASSED\n";
        } catch (const TestFailure& e) {
            g_stats.failed++;
            std::cout << " FAILED\n";
            std::cout << e.what() << "\n\n";
        } catch (const std::exception& e) {
            g_stats.failed++;
            std::cout << " EXCEPTION\n";
            std::cout << "Unexpected exception: " << e.what() << "\n\n";
        }
    }

    std::cout << "\n======================================================================\n";
    std::cout << "TEST SUMMARY\n";
    std::cout << "======================================================================\n";
    std::cout << "Total:  " << g_stats.total << "\n";
    std::cout << "Passed: " << g_stats.passed << "\n";
    std::cout << "Failed: " << g_stats.failed << "\n";
    if (skipped > 0) {
        std::cout << "Skipped: " << skipped << "\n";
    }
    std::cout << "======================================================================\n";

    if (g_stats.failed == 0) {
        std::cout << "\n ALL TESTS PASSED\n\n";
        return 0;
    } else {
        std::cout << "\n SOME TESTS FAILED\n\n";
        return 1;
    }
}

// List all registered tests
inline void ListAllTests() {
    std::cout << "======================================================================\n";
    std::cout << "Available Tests (" << GetTests().size() << " total)\n";
    std::cout << "======================================================================\n";

    std::string current_file;
    for (const auto& test : GetTests()) {
        // Print file header when entering a new test file
        if (test.file != current_file) {
            current_file = test.file;
            std::cout << "\n" << current_file << ":\n";
        }
        std::cout << "  " << test.name << "\n";
    }

    std::cout << "\n======================================================================\n";
}

// Extract filename from full path
inline std::string ExtractFilename(const char* path) {
    std::string str_path(path);
    size_t pos = str_path.find_last_of("/\\");
    if (pos != std::string::npos) {
        return str_path.substr(pos + 1);
    }
    return str_path;
}

// Helper macro for test registration
#define TEST(test_name) \
    void test_##test_name(); \
    namespace { \
        struct TestRegistrar_##test_name { \
            TestRegistrar_##test_name() { \
                UnitTest::RegisterTest(#test_name, UnitTest::ExtractFilename(__FILE__), test_##test_name); \
            } \
        }; \
        static TestRegistrar_##test_name g_registrar_##test_name; \
    } \
    void test_##test_name()

} // namespace UnitTest

#endif // UNIT_TEST_H
