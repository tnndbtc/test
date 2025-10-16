// ============= main.cpp =============
#include "blockweave.h"
#include "wallet/wallet.h"
#include "rest/rest_api.h"
#include "peer/peer.h"
#include "cli/config.h"
#include "cli/daemon.h"
#include "logger/logger.h"
#include "utils/settings.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <limits.h>

void PrintUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -c, --config <file>    Configuration file (default: blockweave.conf)\n";
    std::cout << "  -d, --daemon           Run as daemon process\n";
    std::cout << "  -h, --help             Show this help message\n\n";
    std::cout << "Configuration file (blockweave.conf) should contain:\n";
    std::cout << "  miner_address=<address>\n";
    std::cout << "  rest_api_port=28443\n";
    std::cout << "  daemon=false\n";
}

// Mining thread function
void MiningThread(CBlockweave* p_weave, const std::string& str_miner_address) {
    std::cout << "[Mining Thread] Started\n";

    while (!p_weave->ShouldStopMining()) {
        if (p_weave->IsMiningEnabled() && p_weave->GetMempoolSize() > 0) {
            p_weave->MineBlock(str_miner_address);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    std::cout << "[Mining Thread] Stopped\n";
}

int main(int argc, char* argv[]) {
    std::string str_config_file = "blockweave.conf";
    bool f_daemon_mode = false;

    // Parse command line arguments
    for (int n_i = 1; n_i < argc; n_i++) {
        std::string str_arg = argv[n_i];
        if (str_arg == "-h" || str_arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        }
        else if ((str_arg == "-c" || str_arg == "--config") && n_i + 1 < argc) {
            str_config_file = argv[++n_i];
        }
        else if (str_arg == "-d" || str_arg == "--daemon") {
            f_daemon_mode = true;
        }
        else {
            std::cerr << "Unknown option: " << str_arg << "\n\n";
            PrintUsage(argv[0]);
            return 1;
        }
    }

    // Load configuration
    CConfig config(str_config_file);
    std::string str_miner_address = config.GetMinerAddress();
    int n_rest_port = config.GetRestApiPort();
    int n_p2p_port = config.GetP2PPort();
    std::string str_log_dir = config.GetLogDir();
    std::string str_log_level = config.GetLogLevel();
    std::string str_data_dir = config.GetDataDir();

    // Override daemon mode from command line
    if (f_daemon_mode) {
        config.SetValue("daemon", "true");
    }

    // Validate miner address
    if (str_miner_address.empty()) {
        std::cerr << "Error: miner_address not set in configuration file\n";
        std::cerr << "Please set miner_address in " << str_config_file << "\n";
        return 1;
    }

    // Convert log directory to absolute path (needed for daemon mode)
    char abs_log_dir[PATH_MAX];
    if (str_log_dir[0] != '/') {
        // Relative path - convert to absolute
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            snprintf(abs_log_dir, sizeof(abs_log_dir), "%s/%s", cwd, str_log_dir.c_str());
            str_log_dir = abs_log_dir;
        }
    }

    // Convert data directory to absolute path (needed for daemon mode)
    char abs_data_dir[PATH_MAX];
    if (str_data_dir[0] != '/') {
        // Relative path - convert to absolute
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            snprintf(abs_data_dir, sizeof(abs_data_dir), "%s/%s", cwd, str_data_dir.c_str());
            str_data_dir = abs_data_dir;
            config.SetValue("data_dir", str_data_dir);
        }
    }

    // Setup signal handlers
    CDaemon::SetupSignalHandlers();

    // Daemonize if requested
    if (config.IsDaemonMode()) {
        std::cout << "[Main] Starting in daemon mode...\n";
        std::cout << "[Main] Log directory: " << str_log_dir << "\n";
        if (!CDaemon::Daemonize("/tmp/rest_daemon.pid")) {
            std::cerr << "Failed to daemonize process\n";
            return 1;
        }
        // After daemonization, file descriptors are closed and working directory is /
        // So we must initialize logger after daemonization with absolute path
    }

    // Initialize logger (after daemonization to avoid file descriptor issues)
    ELogLevel log_level = ParseLogLevelString(str_log_level);
    if (!InitializeLogger(str_log_dir, log_level)) {
        // In daemon mode, stderr is redirected to /dev/null, so this won't be seen
        // But in non-daemon mode, user will see the error
        std::cerr << "Error: Failed to initialize logger\n";
        return 1;
    }
    LOG_INFO("=== Blockweave REST Daemon Starting ===");
    if (config.IsDaemonMode()) {
        LOG_INFO("Daemon process started successfully");
    }
    LOG_INFO("Log level set to: " + str_log_level);

    std::cout << "=== Blockweave REST Daemon ===\n\n";
    std::cout << "Miner address: " << str_miner_address.substr(0, 16) << "...\n";
    std::cout << "REST API port: " << n_rest_port << "\n";
    std::cout << "P2P port: " << n_p2p_port << "\n";
    std::cout << "REST worker threads: " << REST_WORKER_THREADS << "\n\n";

    LOG_INFO("Configuration loaded:");
    LOG_INFO("  Miner address: " + str_miner_address.substr(0, 16) + "...");
    LOG_INFO("  REST API port: " + std::to_string(n_rest_port));
    LOG_INFO("  P2P port: " + std::to_string(n_p2p_port));
    LOG_INFO("  REST worker threads: " + std::to_string(REST_WORKER_THREADS));
    LOG_INFO("  Log directory: " + str_log_dir);

    CBlockweave weave;
    LOG_INFO("Blockweave instance created");

    // Start REST API server (1 listener thread + N worker threads)
    LOG_INFO("Starting REST API server on port " + std::to_string(n_rest_port));
    CRestApiServer rest_api(&weave, &config, str_miner_address, n_rest_port);
    if (!rest_api.Start()) {
        std::cerr << "Failed to start REST API server\n";
        LOG_ERROR("Failed to start REST API server on port " + std::to_string(n_rest_port));
        return 1;
    }
    LOG_INFO("REST API server started successfully");

    // Start peer manager
    LOG_INFO("Starting peer manager on port " + std::to_string(n_p2p_port));
    CPeerManager peer_manager(n_p2p_port);
    if (!peer_manager.Start()) {
        std::cerr << "Failed to start peer manager\n";
        LOG_ERROR("Failed to start peer manager on port " + std::to_string(n_p2p_port));
        rest_api.Stop();
        return 1;
    }
    LOG_INFO("Peer manager started successfully");

    // Start mining thread
    weave.StartMining();
    LOG_INFO("Mining enabled");
    std::thread mining_thread(MiningThread, &weave, str_miner_address);
    LOG_INFO("Mining thread started");

    std::cout << "[Main] REST daemon is running. Press Ctrl+C to stop.\n";
    std::cout << "[Main] Use REST API on port " << n_rest_port << " to interact with the blockchain.\n";
    std::cout << "[Main] P2P network listening on port " << n_p2p_port << "\n\n";
    LOG_INFO("REST daemon is running and ready to accept requests");

    // Main loop - wait for shutdown signal
    while (!g_f_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\n[Main] Shutdown signal received. Cleaning up...\n";
    LOG_INFO("Shutdown signal received, initiating graceful shutdown");

    // Stop mining and wait for thread to finish
    LOG_INFO("Stopping mining thread");
    weave.StopMining();
    mining_thread.join();
    LOG_INFO("Mining thread stopped");

    // Stop peer manager
    LOG_INFO("Stopping peer manager");
    peer_manager.Stop();
    LOG_INFO("Peer manager stopped");

    // Stop REST API server
    LOG_INFO("Stopping REST API server");
    rest_api.Stop();
    LOG_INFO("REST API server stopped");

    // Print final state
    weave.PrintChain();

    // Cleanup PID file if in daemon mode
    if (config.IsDaemonMode()) {
        CDaemon::RemovePidFile("/tmp/rest_daemon.pid");
        LOG_INFO("PID file removed");
    }

    std::cout << "[Main] Shutdown complete.\n";
    LOG_INFO("Shutdown complete");

    // Flush logger before exit
    if (g_p_logger) {
        g_p_logger->Flush();
    }

    return 0;
}

