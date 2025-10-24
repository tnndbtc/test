// ============= test_hash.cpp =============
#include "unit_test.h"
#include "utils/hash.h"

using namespace UnitTest;

/**
 * @brief Test CHash constructor and data retrieval
 */
TEST(Hash_Constructor) {
    CHash hash1("test_data");
    CHash hash2("test_data");
    CHash hash3("different_data");

    ASSERT_EQUAL(hash1.GetData(), hash2.GetData(), "Same input should produce same hash");
    ASSERT_NOT_EQUAL(hash1.GetData(), hash3.GetData(), "Different input should produce different hash");
}

/**
 * @brief Test CHash empty input
 */
TEST(Hash_EmptyInput) {
    CHash hash("");

    ASSERT_TRUE(hash.GetData().length() > 0, "Hash of empty string should still produce output");
}
