// ============= mining.cpp =============
/**
 * @file mining.cpp
 * @brief Implementation of mining thread management
 *
 * Provides mining thread that continuously attempts to mine new blocks
 * when enabled and transactions are available in the mempool.
 */

#include "mining.h"
#include "utils/threadname.h"
#include "logger/logger.h"
#include <chrono>

/**
 * @brief Constructor - initialize mining manager
 * @param p_weave Pointer to blockweave instance
 * @param str_miner_addr Mining reward address
 *
 * Initializes mining manager in stopped state.
 */
CMiningManager::CMiningManager(CBlockweave* p_weave, const std::string& str_miner_addr)
    : p_blockweave(p_weave), str_miner_address(str_miner_addr), f_running(false) {
}

/**
 * @brief Destructor - stops mining if running
 *
 * Ensures mining thread is properly stopped and joined.
 * Safe to call even if mining was never started.
 */
CMiningManager::~CMiningManager() {
    Stop();
}

/**
 * @brief Start mining thread
 * @return true if started successfully, false if already running
 *
 * Starts background mining thread. Returns false if already running.
 * Thread is named "mining_thread" for easier debugging.
 */
bool CMiningManager::Start() {
    if (f_running) {
        LOG_WARN("Mining manager already running");
        return false;
    }

    f_running = true;
    m_mining_thread = std::thread(&CMiningManager::MiningThread, this);

    LOG_INFO("Mining manager started");
    return true;
}

/**
 * @brief Stop mining thread
 *
 * Signals blockweave to stop mining and waits for thread to exit.
 * Thread-safe and idempotent (safe to call multiple times).
 */
void CMiningManager::Stop() {
    if (!f_running) {
        return;
    }

    LOG_INFO("Stopping mining manager");
    f_running = false;

    // Signal blockweave to stop mining
    if (p_blockweave) {
        p_blockweave->StopMining();
    }

    // Wait for mining thread to finish
    if (m_mining_thread.joinable()) {
        m_mining_thread.join();
    }

    LOG_INFO("Mining manager stopped");
}

/**
 * @brief Check if mining manager is running
 * @return true if running, false otherwise
 */
bool CMiningManager::IsRunning() const {
    return f_running;
}

/**
 * @brief Mining thread function - continuously mines blocks
 *
 * Main mining loop that:
 * 1. Checks if mining is enabled via blockweave->IsMiningEnabled()
 * 2. Checks if transactions available via blockweave->GetMempoolSize()
 * 3. Mines a block if conditions are met
 * 4. Sleeps between attempts (500ms after mining, 100ms when idle)
 *
 * Thread is named "mining_thread" for visibility in logs and debuggers.
 * Exits when blockweave->ShouldStopMining() returns true.
 */
void CMiningManager::MiningThread() {
    // Set thread name for easier debugging and logging
    SetThreadName("mining_thread");

    LOG_INFO("Mining thread started");

    while (!p_blockweave->ShouldStopMining()) {
        if (p_blockweave->IsMiningEnabled() && p_blockweave->GetMempoolSize() > 0) {
            // Mine a new block with miner address for rewards
            p_blockweave->MineBlock(str_miner_address);

            // Sleep after mining to prevent CPU saturation
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } else {
            // Mining disabled or no transactions - sleep and check again
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    LOG_INFO("Mining thread stopped");
}
