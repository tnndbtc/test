// ============= test_get_public_ip.cpp =============
#include "unit_test.h"
#include "utils/get_public_ip.h"
#include <vector>
#include <string>

using namespace UnitTest;

/**
 * @brief Test basic STUN query with real server
 *
 * Tests that GetPublicIP() can successfully query a real STUN server
 * and return a valid public IP address (non-loopback).
 */
TEST(GetPublicIP_BasicQuery) {
    std::vector<std::string> stun_servers = {
        "stun.l.google.com:19302"
    };

    std::string public_ip = GetPublicIP(stun_servers);

    // Should return non-loopback IP
    ASSERT_TRUE(public_ip != "127.0.0.1", "Should discover non-loopback IP");
    ASSERT_TRUE(public_ip.find('.') != std::string::npos, "Should be IPv4 format");
}

/**
 * @brief Test fallback to next server on failure
 *
 * Tests that GetPublicIP() tries multiple servers in order and succeeds
 * with the first working server.
 */
TEST(GetPublicIP_Fallback) {
    std::vector<std::string> stun_servers = {
        "invalid.host.example.com:19302",  // Should fail
        "stun.l.google.com:19302"          // Should succeed
    };

    std::string public_ip = GetPublicIP(stun_servers);

    ASSERT_TRUE(public_ip != "127.0.0.1", "Should discover public IP from second server");
}

/**
 * @brief Test all servers fail
 *
 * Tests that GetPublicIP() returns "127.0.0.1" when all servers fail to respond.
 */
TEST(GetPublicIP_AllFail) {
    std::vector<std::string> stun_servers = {
        "invalid1.example.com:19302",
        "invalid2.example.com:19302"
    };

    std::string public_ip = GetPublicIP(stun_servers, 1000);  // Short timeout

    ASSERT_EQUAL(public_ip, std::string("127.0.0.1"), "Should return 127.0.0.1 when all fail");
}

/**
 * @brief Test empty server list
 *
 * Tests that GetPublicIP() returns "127.0.0.1" when given an empty server list.
 */
TEST(GetPublicIP_EmptyList) {
    std::vector<std::string> stun_servers;

    std::string public_ip = GetPublicIP(stun_servers);

    ASSERT_EQUAL(public_ip, std::string("127.0.0.1"), "Should return 127.0.0.1 for empty list");
}

/**
 * @brief Test timeout handling
 *
 * Tests that GetPublicIP() properly handles timeouts when contacting
 * unreachable servers (TEST-NET-1 address).
 */

TEST(GetPublicIP_Timeout) {
    std::vector<std::string> stun_servers = {
        "192.0.2.1:19302"  // TEST-NET-1, should timeout
    };

    std::string public_ip = GetPublicIP(stun_servers, 1000);  // 1 second timeout

    ASSERT_EQUAL(public_ip, std::string("127.0.0.1"), "Should return 127.0.0.1 on timeout");
}

/**
 * @brief Test multiple working servers
 *
 * Tests that GetPublicIP() returns successfully when given multiple
 * working STUN servers (should use the first one).
 */
TEST(GetPublicIP_MultipleServers) {
    std::vector<std::string> stun_servers = {
        "stun.l.google.com:19302",
        "stun1.l.google.com:19302",
        "stun.stunprotocol.org:3478"
    };

    std::string public_ip = GetPublicIP(stun_servers);

    ASSERT_TRUE(public_ip != "127.0.0.1", "Should discover public IP");
    ASSERT_TRUE(public_ip.find('.') != std::string::npos, "Should be IPv4 format");
}
