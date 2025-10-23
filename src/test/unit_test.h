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
    std::function<void()> test_func;
};

// Test registry
inline std::vector<TestCase>& GetTests() {
    static std::vector<TestCase> tests;
    return tests;
}

// Register a test
inline void RegisterTest(const std::string& name, std::function<void()> test_func) {
    GetTests().push_back({name, test_func});
}

// Run all registered tests
inline int RunAllTests() {
    std::cout << "======================================================================\n";
    std::cout << "Running Unit Tests\n";
    std::cout << "======================================================================\n\n";

    for (const auto& test : GetTests()) {
        g_stats.total++;
        std::cout << "Running: " << test.name << " ... ";

        try {
            test.test_func();
            g_stats.passed++;
            std::cout << "\033[32m✓ PASSED\033[0m\n";
        } catch (const TestFailure& e) {
            g_stats.failed++;
            std::cout << "\033[31m✗ FAILED\033[0m\n";
            std::cout << e.what() << "\n\n";
        } catch (const std::exception& e) {
            g_stats.failed++;
            std::cout << "\033[31m✗ EXCEPTION\033[0m\n";
            std::cout << "Unexpected exception: " << e.what() << "\n\n";
        }
    }

    std::cout << "\n======================================================================\n";
    std::cout << "TEST SUMMARY\n";
    std::cout << "======================================================================\n";
    std::cout << "Total:  " << g_stats.total << "\n";
    std::cout << "Passed: \033[32m" << g_stats.passed << "\033[0m\n";
    std::cout << "Failed: \033[31m" << g_stats.failed << "\033[0m\n";
    std::cout << "======================================================================\n";

    if (g_stats.failed == 0) {
        std::cout << "\n\033[32m✓ ALL TESTS PASSED\033[0m\n\n";
        return 0;
    } else {
        std::cout << "\n\033[31m✗ SOME TESTS FAILED\033[0m\n\n";
        return 1;
    }
}

// Helper macro for test registration
#define TEST(test_name) \
    void test_##test_name(); \
    namespace { \
        struct TestRegistrar_##test_name { \
            TestRegistrar_##test_name() { \
                UnitTest::RegisterTest(#test_name, test_##test_name); \
            } \
        }; \
        static TestRegistrar_##test_name g_registrar_##test_name; \
    } \
    void test_##test_name()

} // namespace UnitTest

#endif // UNIT_TEST_H
