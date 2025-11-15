// ============= mining.h =============
/**
 * @file mining.h
 * @brief Mining thread management for blockweave
 *
 * Provides mining thread functionality that continuously mines new blocks
 * when enabled and transactions are available in the mempool.
 */

#ifndef MINING_H
#define MINING_H

#include "block_weave.h"
#include "../utils/network.h"
#include <string>
#include <thread>
#include <atomic>

/**
 * @class CMiningManager
 * @brief Manages the mining thread lifecycle
 *
 * Encapsulates mining thread management, allowing starting, stopping,
 * and monitoring the mining process. The mining thread continuously
 * attempts to mine new blocks when mining is enabled and transactions
 * are available in the mempool.
 *
 * Architecture:
 * - Single mining thread runs in background
 * - Thread checks mining enabled flag and mempool status
 * - Sleeps when mining disabled or no transactions available
 * - Uses miner address for block rewards
 *
 * Thread safety:
 * - Thread-safe access to blockweave (blockweave has internal mutex)
 * - Atomic flags for state management
 *
 * Example usage:
 *   CMiningManager mining(&blockweave, "miner_address");
 *   mining.Start();
 *   // ... mining runs in background ...
 *   mining.Stop();
 */
class CMiningManager {
private:
    CBlockweave* p_blockweave;          ///< Pointer to blockweave instance
    std::string str_miner_address;      ///< Mining reward address
    NetworkType m_network_type;         ///< Network type (localnet/testnet/mainnet)
    std::thread m_mining_thread;        ///< Mining thread handle
    std::atomic<bool> f_running;        ///< Mining manager running state

    /**
     * @brief Mining thread function - continuously mines blocks
     *
     * Main mining loop:
     * 1. Check if mining is enabled via blockweave
     * 2. Check if transactions available in mempool
     * 3. Mine block if conditions met
     * 4. Sleep between mining attempts
     *
     * Mining interval:
     * - 500ms sleep after successful mining
     * - 100ms sleep when mining disabled or no transactions
     *
     * Exits when blockweave signals mining should stop.
     */
    void MiningThread();

public:
    /**
     * @brief Constructor - initialize mining manager
     * @param p_weave Pointer to blockweave instance
     * @param str_miner_addr Mining reward address
     * @param network_type Network type (localnet/testnet/mainnet)
     *
     * Creates mining manager in stopped state.
     * Call Start() to begin mining.
     *
     * Note: On localnet, automatic mining is disabled (only manual mining via RPC).
     */
    CMiningManager(CBlockweave* p_weave, const std::string& str_miner_addr, NetworkType network_type);

    /**
     * @brief Destructor - stops mining if running
     *
     * Ensures mining thread is properly stopped and joined.
     * Safe to call even if mining was never started.
     */
    ~CMiningManager();

    /**
     * @brief Start mining thread
     * @return true if started successfully, false if already running
     *
     * Starts background mining thread with named thread ID.
     * Thread will run until Stop() is called or blockweave
     * requests shutdown.
     */
    bool Start();

    /**
     * @brief Stop mining thread
     *
     * Signals mining thread to stop and waits for it to exit.
     * Thread-safe and idempotent (safe to call multiple times).
     * Blocks until mining thread has terminated.
     */
    void Stop();

    /**
     * @brief Check if mining manager is running
     * @return true if mining thread is active, false otherwise
     */
    bool IsRunning() const;
};

#endif
