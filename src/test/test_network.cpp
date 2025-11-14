// ============= test_network.cpp =============
#include "unit_test.h"
#include "utils/network.h"
#include "utils/config.h"

using namespace UnitTest;

/**
 * @brief Test ParseNetworkType with valid inputs
 */
TEST(Network_ParseNetworkType_Valid) {
    ASSERT_EQUAL(static_cast<int>(ParseNetworkType("mainnet")), static_cast<int>(NetworkType::MAINNET), "mainnet should parse correctly");
    ASSERT_EQUAL(static_cast<int>(ParseNetworkType("MAINNET")), static_cast<int>(NetworkType::MAINNET), "MAINNET (uppercase) should parse correctly");
    ASSERT_EQUAL(static_cast<int>(ParseNetworkType("testnet")), static_cast<int>(NetworkType::TESTNET), "testnet should parse correctly");
    ASSERT_EQUAL(static_cast<int>(ParseNetworkType("TESTNET")), static_cast<int>(NetworkType::TESTNET), "TESTNET (uppercase) should parse correctly");
    ASSERT_EQUAL(static_cast<int>(ParseNetworkType("localnet")), static_cast<int>(NetworkType::LOCALNET), "localnet should parse correctly");
    ASSERT_EQUAL(static_cast<int>(ParseNetworkType("LOCALNET")), static_cast<int>(NetworkType::LOCALNET), "LOCALNET (uppercase) should parse correctly");
}

/**
 * @brief Test ParseNetworkType with invalid input throws exception
 */
TEST(Network_ParseNetworkType_Invalid) {
    bool f_exception_thrown = false;
    try {
        ParseNetworkType("invalid");
    } catch (const std::runtime_error& e) {
        f_exception_thrown = true;
    }
    ASSERT_TRUE(f_exception_thrown, "Invalid network type should throw exception");
}

/**
 * @brief Test NetworkTypeToString
 */
TEST(Network_NetworkTypeToString) {
    ASSERT_EQUAL(NetworkTypeToString(NetworkType::MAINNET), std::string("mainnet"), "MAINNET should convert to 'mainnet'");
    ASSERT_EQUAL(NetworkTypeToString(NetworkType::TESTNET), std::string("testnet"), "TESTNET should convert to 'testnet'");
    ASSERT_EQUAL(NetworkTypeToString(NetworkType::LOCALNET), std::string("localnet"), "LOCALNET should convert to 'localnet'");
}

/**
 * @brief Test GetNetworkParams for mainnet
 */
TEST(Network_GetNetworkParams_Mainnet) {
    const NetworkParams& params = GetNetworkParams(NetworkType::MAINNET);

    ASSERT_EQUAL(params.network_name, std::string("mainnet"), "Network name should be 'mainnet'");
    ASSERT_EQUAL(params.magic_bytes, MAINNET_MAGIC, "Magic bytes should match MAINNET_MAGIC");
    ASSERT_EQUAL(params.default_p2p_port, 28333, "P2P port should be 28333");
    ASSERT_EQUAL(params.default_rest_port, 28443, "REST port should be 28443");
    // ASSERT_EQUAL(params.target_block_time_seconds, 600, "Block time should be 600s");
    ASSERT_FALSE(params.fast_mining, "Fast mining should be disabled");
}

/**
 * @brief Test GetNetworkParams for testnet
 */
TEST(Network_GetNetworkParams_Testnet) {
    const NetworkParams& params = GetNetworkParams(NetworkType::TESTNET);

    ASSERT_EQUAL(params.network_name, std::string("testnet"), "Network name should be 'testnet'");
    ASSERT_EQUAL(params.magic_bytes, TESTNET_MAGIC, "Magic bytes should match TESTNET_MAGIC");
    ASSERT_EQUAL(params.default_p2p_port, 38333, "P2P port should be 38333");
    ASSERT_EQUAL(params.default_rest_port, 38443, "REST port should be 38443");
    // ASSERT_EQUAL(params.target_block_time_seconds, 60, "Block time should be 60s");
    ASSERT_FALSE(params.fast_mining, "Fast mining should be disabled");
}

/**
 * @brief Test GetNetworkParams for localnet
 */
TEST(Network_GetNetworkParams_Localnet) {
    const NetworkParams& params = GetNetworkParams(NetworkType::LOCALNET);

    ASSERT_EQUAL(params.network_name, std::string("localnet"), "Network name should be 'localnet'");
    ASSERT_EQUAL(params.magic_bytes, LOCALNET_MAGIC, "Magic bytes should match LOCALNET_MAGIC");
    ASSERT_EQUAL(params.default_p2p_port, 48333, "P2P port should be 48333");
    ASSERT_EQUAL(params.default_rest_port, 48443, "REST port should be 48443");
    // ASSERT_EQUAL(params.target_block_time_seconds, 1, "Block time should be 1s");
    ASSERT_TRUE(params.fast_mining, "Fast mining should be enabled");
}

/**
 * @brief Test that magic bytes are unique across networks
 */
TEST(Network_MagicBytes_Unique) {
    const NetworkParams& mainnet = GetNetworkParams(NetworkType::MAINNET);
    const NetworkParams& testnet = GetNetworkParams(NetworkType::TESTNET);
    const NetworkParams& localnet = GetNetworkParams(NetworkType::LOCALNET);

    ASSERT_NOT_EQUAL(mainnet.magic_bytes, testnet.magic_bytes, "Mainnet and testnet magic bytes should differ");
    ASSERT_NOT_EQUAL(mainnet.magic_bytes, localnet.magic_bytes, "Mainnet and localnet magic bytes should differ");
    ASSERT_NOT_EQUAL(testnet.magic_bytes, localnet.magic_bytes, "Testnet and localnet magic bytes should differ");
}

/**
 * @brief Test CConfig default network
 */
TEST(Config_Network_Default) {
    CConfig config;
    std::string str_network = config.GetNetwork();
    ASSERT_EQUAL(str_network, std::string("mainnet"), "Default network should be mainnet");
}

/**
 * @brief Test CConfig SetValue for network
 */
TEST(Config_Network_SetValue) {
    CConfig config;

    config.SetValue("network", "testnet");
    ASSERT_EQUAL(config.GetNetwork(), std::string("testnet"), "Network should change to testnet");

    config.SetValue("network", "localnet");
    ASSERT_EQUAL(config.GetNetwork(), std::string("localnet"), "Network should change to localnet");

    config.SetValue("network", "mainnet");
    ASSERT_EQUAL(config.GetNetwork(), std::string("mainnet"), "Network should change back to mainnet");
}

/**
 * @brief Test CConfig GetNetworkDataDir with relative paths
 * Note: Relative paths are now expanded to ~/.bweave/ by default
 */
TEST(Config_Network_DataDir_Relative) {
    CConfig config;
    config.SetValue("data_dir", "data");  // Use "data" not "./data"

    std::string mainnet_dir = config.GetNetworkDataDir("mainnet");
    // Path should be expanded to ~/.bweave/data/mainnet
    // Note: On Windows, GetNetworkDataDir uses forward slash for network name, creating mixed paths
    ASSERT_TRUE(mainnet_dir.find("/.bweave/data/mainnet") != std::string::npos ||
                mainnet_dir.find("\\bweave\\data/mainnet") != std::string::npos,
                "Mainnet data dir should be under ~/.bweave/");

    std::string testnet_dir = config.GetNetworkDataDir("testnet");
    ASSERT_TRUE(testnet_dir.find("/.bweave/data/testnet") != std::string::npos ||
                testnet_dir.find("\\bweave\\data/testnet") != std::string::npos,
                "Testnet data dir should be under ~/.bweave/");

    std::string localnet_dir = config.GetNetworkDataDir("localnet");
    ASSERT_TRUE(localnet_dir.find("/.bweave/data/localnet") != std::string::npos ||
                localnet_dir.find("\\bweave\\data/localnet") != std::string::npos,
                "Localnet data dir should be under ~/.bweave/");
}

/**
 * @brief Test CConfig GetNetworkDataDir with trailing slash
 */
TEST(Config_Network_DataDir_TrailingSlash) {
    CConfig config;
    config.SetValue("data_dir", "data/");  // Trailing slash

    std::string mainnet_dir = config.GetNetworkDataDir("mainnet");
    // Should expand to ~/.bweave/data/mainnet (trailing slash removed)
    // Note: On Windows, GetNetworkDataDir uses forward slash for network name, creating mixed paths
    ASSERT_TRUE(mainnet_dir.find("/.bweave/data/mainnet") != std::string::npos ||
                mainnet_dir.find("\\bweave\\data/mainnet") != std::string::npos,
                "Trailing slash should be handled correctly");
}

/**
 * @brief Test CConfig GetNetworkDataDir with absolute path
 */
TEST(Config_Network_DataDir_Absolute) {
    CConfig config;
#ifdef _WIN32
    // Windows absolute path with drive letter
    config.SetValue("data_dir", "C:\\blockweave\\data");
    std::string testnet_dir = config.GetNetworkDataDir("testnet");
    ASSERT_EQUAL(testnet_dir, std::string("C:\\blockweave\\data/testnet"), "Absolute path should work correctly");
#else
    // Unix absolute path
    config.SetValue("data_dir", "/var/blockweave/data");
    std::string testnet_dir = config.GetNetworkDataDir("testnet");
    ASSERT_EQUAL(testnet_dir, std::string("/var/blockweave/data/testnet"), "Absolute path should work correctly");
#endif
}

/**
 * @brief Test network parameters roundtrip (parse -> get params -> convert back)
 */
TEST(Network_Roundtrip) {
    NetworkType type = ParseNetworkType("testnet");
    const NetworkParams& params = GetNetworkParams(type);
    std::string str_name = NetworkTypeToString(type);

    ASSERT_EQUAL(str_name, std::string("testnet"), "Roundtrip should preserve network name");
    ASSERT_EQUAL(params.network_name, std::string("testnet"), "Params should match parsed type");
}
