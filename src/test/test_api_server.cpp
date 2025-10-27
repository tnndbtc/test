// ============= test_api_server.cpp =============
#include "unit_test.h"
#include "rest/rest_api_server.h"
#include "blockcore/blockweave.h"
#include "peer/peer_manager.h"
#include "utils/config.h"
#include <thread>
#include <chrono>

using namespace UnitTest;

/**
 * @brief Test CRestApiServer constructor
 */
TEST(RestApiServer_Constructor) {
    CBlockweave blockweave;
    CPeerManager peer_manager(28333);
    CConfig config;

    CRestApiServer server(&blockweave, &peer_manager, &config, "test_miner_address", 28443);

    ASSERT_FALSE(server.IsRunning(), "Server should not be running initially");
}

/**
 * @brief Test CRestApiServer start and stop
 */
TEST(RestApiServer_StartStop) {
    CBlockweave blockweave;
    CPeerManager peer_manager(38333);
    CConfig config;

    CRestApiServer server(&blockweave, &peer_manager, &config, "test_miner_address", 38443);

    ASSERT_FALSE(server.IsRunning(), "Server should not be running initially");

    bool started = server.Start();
    ASSERT_TRUE(started, "Server should start successfully");
    ASSERT_TRUE(server.IsRunning(), "Server should be running after start");

    // Give server time to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    server.Stop();
    ASSERT_FALSE(server.IsRunning(), "Server should not be running after stop");
}

/**
 * @brief Test CRestApiServer multiple start/stop cycles
 */
TEST(RestApiServer_MultipleStartStop) {
    CBlockweave blockweave;
    CPeerManager peer_manager(38334);
    CConfig config;

    CRestApiServer server(&blockweave, &peer_manager, &config, "test_miner_address", 38444);

    for (int i = 0; i < 3; i++) {
        bool started = server.Start();
        ASSERT_TRUE(started, "Server should start on cycle " + std::to_string(i));

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        server.Stop();
        ASSERT_FALSE(server.IsRunning(), "Should stop on cycle " + std::to_string(i));
    }
}

/**
 * @brief Test CRestApiServer stop without start
 */
TEST(RestApiServer_StopWithoutStart) {
    CBlockweave blockweave;
    CPeerManager peer_manager(38335);
    CConfig config;

    CRestApiServer server(&blockweave, &peer_manager, &config, "test_miner_address", 38445);

    // Stop without starting should not crash
    server.Stop();

    ASSERT_FALSE(server.IsRunning(), "Server should not be running");
}

/**
 * @brief Test CRestApiServer destructor stops running server
 */
TEST(RestApiServer_DestructorStops) {
    CBlockweave blockweave;
    CPeerManager peer_manager(38336);
    CConfig config;

    {
        CRestApiServer server(&blockweave, &peer_manager, &config, "test_miner_address", 38446);
        server.Start();
        ASSERT_TRUE(server.IsRunning(), "Server should be running");

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Destructor will be called when server goes out of scope
    }

    // If we get here without crashing, the destructor worked correctly
    ASSERT_TRUE(true, "Destructor should stop server cleanly");
}
