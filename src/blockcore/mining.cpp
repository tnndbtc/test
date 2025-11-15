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
#include "utils/settings.h"
#include "logger/logger.h"
#include <chrono>

/**
 * @brief Constructor - initialize mining manager
 * @param p_weave Pointer to blockweave instance
 * @param str_miner_addr Mining reward address
 * @param network_type Network type (localnet/testnet/mainnet)
 *
 * Initializes mining manager in stopped state.
 */
CMiningManager::CMiningManager(CBlockweave* p_weave, const std::string& str_miner_addr, NetworkType network_type)
    : p_blockweave(p_weave), str_miner_address(str_miner_addr), m_network_type(network_type), f_running(false) {
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
 * 2. If mempool has transactions, mines every MINING_INTERVAL_SECONDS (30 seconds)
 * 3. If mempool is empty, mines an empty block every MINING_EMPTY_BLOCK_INTERVAL_SECONDS (24 hours)
 * 4. Both intervals are configurable in settings.h
 *
 * IMPORTANT: On localnet, automatic mining is DISABLED. Blocks can only be mined
 * manually via /rpc/minetrigger. On mainnet and testnet, automatic time-based
 * mining continues as normal.
 *
 * Thread is named "mining_thread" for visibility in logs and debuggers.
 * Exits when blockweave->ShouldStopMining() returns true.
 */
void CMiningManager::MiningThread() {
    // Set thread name for easier debugging and logging
    SetThreadName("mining_thread");

    LOG_INFO("Mining thread started");

    // On localnet, disable automatic mining - only manual mining via /rpc/minetrigger
    if (m_network_type == NetworkType::LOCALNET) {
        LOG_INFO("Localnet detected - automatic mining disabled (use /rpc/minetrigger for manual mining)");
        while (!p_blockweave->ShouldStopMining()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        LOG_INFO("Mining thread stopped");
        return;
    }

    // Track last empty block mining time
    auto last_empty_block_time = std::chrono::steady_clock::now();
    auto last_mining_time = std::chrono::steady_clock::now();

    while (!p_blockweave->ShouldStopMining()) {
        if (p_blockweave->IsMiningEnabled()) {
            auto now = std::chrono::steady_clock::now();
            size_t n_mempool_size = p_blockweave->GetMempoolSize();

            if (n_mempool_size > 0) {
                // Mempool has transactions - mine every MINING_INTERVAL_SECONDS
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_mining_time).count();

                if (elapsed >= MINING_INTERVAL_SECONDS) {
                    LOG_INFO("Mining block with " + std::to_string(n_mempool_size) + " transactions in mempool");
                    p_blockweave->MineBlock(str_miner_address);
                    last_mining_time = now;
                    last_empty_block_time = now; // Reset empty block timer
                }
            } else {
                // Mempool is empty - mine empty block every MINING_EMPTY_BLOCK_INTERVAL_SECONDS
                auto elapsed_empty = std::chrono::duration_cast<std::chrono::seconds>(now - last_empty_block_time).count();

                if (elapsed_empty >= MINING_EMPTY_BLOCK_INTERVAL_SECONDS) {
                    LOG_INFO("Mining empty block (mempool is empty, interval: " +
                             std::to_string(MINING_EMPTY_BLOCK_INTERVAL_SECONDS) + " seconds)");
                    p_blockweave->MineBlock(str_miner_address);
                    last_empty_block_time = now;
                    last_mining_time = now;
                }
            }
        }

        // Sleep for a short interval to check conditions
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    LOG_INFO("Mining thread stopped");
}
