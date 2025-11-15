// ============= main.cpp =============
#include "blockcore/block_weave.h"
#include "blockcore/daemon.h"
#include "blockcore/mining.h"
#include "wallet/wallet.h"
#include "rest/rest_api_server.h"
#include "peer/peer_manager.h"
#include "utils/config.h"
#include "utils/network.h"
#include "utils/threadname.h"
#include "logger/logger.h"
#include "utils/settings.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>
#include <sys/stat.h>

#ifdef _WIN32
    #include <direct.h>  // for _getcwd on Windows
    #include <windows.h>
    #include "blockcore/win_service.h"  // Windows service support
    #ifndef PATH_MAX
        #define PATH_MAX MAX_PATH
    #endif
    #define getcwd _getcwd
#else
    #include <unistd.h>
    #include <limits.h>
#endif

// Forward declaration
int RunApplicationMain(const std::string& str_config_file, bool f_daemon_mode, const std::string& str_network);

void PrintUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -c, --config <file>    Configuration file (default: bweave.conf)\n";
    std::cout << "  -d, --daemon           Run as daemon process\n";
    std::cout << "  --network <type>       Network type: mainnet|testnet|localnet (default: mainnet)\n";
    std::cout << "  -h, --help             Show this help message\n";
#ifdef _WIN32
    std::cout << "\nWindows Service Options:\n";
    std::cout << "  --install-service      Install as Windows service\n";
    std::cout << "  --uninstall-service    Uninstall Windows service\n";
    std::cout << "  --service              Run as Windows service (internal use)\n";
#endif
    std::cout << "\nConfiguration file (bweave.conf) should contain:\n";
    std::cout << "  miner_address=<address>\n";
    std::cout << "  rest_api_port=28443\n";
    std::cout << "  daemon=false\n";
}

int main(int argc, char* argv[]) {
    // Set main thread name for easier debugging and logging
    SetThreadName("main_thread");

#ifdef _WIN32
    // Windows: Handle service installation/uninstallation/starting first
    for (int n_i = 1; n_i < argc; n_i++) {
        std::string str_arg = argv[n_i];

        if (str_arg == "--install-service") {
            // Get executable path
            char exe_path[MAX_PATH];
            if (GetModuleFileNameA(NULL, exe_path, MAX_PATH) == 0) {
                std::cerr << "[Service] Failed to get executable path\n";
                return 1;
            }
            return InstallService(exe_path) ? 0 : 1;
        }
        else if (str_arg == "--uninstall-service") {
            return UninstallService() ? 0 : 1;
        }
        else if (str_arg == "--service") {
            // Running as Windows service - start service dispatcher
            if (StartServiceDispatcher(argc, argv)) {
                return 0;  // Service completed successfully
            } else {
                return 1;  // Service failed
            }
        }
    }

    // Windows: Removed automatic service detection
    // Service mode MUST be explicitly requested via --service flag
    // Automatic detection via IsRunningAsService() is unreliable when
    // stdout/stderr are redirected (e.g., from Python subprocess, systemd, etc.)

    // Windows console mode: Setup console control handler for Ctrl+C
    if (!SetupConsoleCtrlHandler()) {
        std::cerr << "[Console] Warning: Failed to setup console control handler\n";
        // Continue anyway - not fatal
    }
#endif

    std::string str_config_file = "bweave.conf";
    bool f_daemon_mode = false;
    std::string str_network = "";  // Empty means use config file value

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
        else if (str_arg == "--network" && n_i + 1 < argc) {
            str_network = argv[++n_i];
            // Validate network type
            if (str_network != "mainnet" && str_network != "testnet" && str_network != "localnet") {
                std::cerr << "Error: Invalid network type '" << str_network << "'\n";
                std::cerr << "Valid options: mainnet, testnet, localnet\n\n";
                PrintUsage(argv[0]);
                return 1;
            }
        }
#ifdef _WIN32
        else if (str_arg == "--service" || str_arg == "--install-service" || str_arg == "--uninstall-service") {
            // Already handled above - skip
            continue;
        }
#endif
        else {
            std::cerr << "Unknown option: " << str_arg << "\n\n";
            PrintUsage(argv[0]);
            return 1;
        }
    }

    // Run the main application logic
    return RunApplicationMain(str_config_file, f_daemon_mode, str_network);
}

/**
 * @brief Main application logic - runs the daemon
 * @param str_config_file Path to configuration file
 * @param f_daemon_mode true to run as daemon (POSIX only)
 * @param str_network Network type from command line (empty = use config)
 * @return Exit code (0 for success)
 *
 * This function contains the main application logic and is called from:
 * - main() in console mode (Windows and POSIX)
 * - ServiceMain() via RunApplication() in Windows service mode
 */
int RunApplicationMain(const std::string& str_config_file, bool f_daemon_mode, const std::string& str_network) {
    // Load configuration
    CConfig config(str_config_file);

    // Override network from command line if specified
    if (!str_network.empty()) {
        config.SetValue("network", str_network);
    }

    // Get network configuration and parameters
    std::string str_configured_network = config.GetNetwork();
    NetworkType network_type = ParseNetworkType(str_configured_network);
    const NetworkParams& network_params = GetNetworkParams(network_type);

    std::string str_miner_address = config.GetMinerAddress();

    // Use network-specific default ports if not set in config
    int n_rest_port = config.GetRestApiPort();
    int n_p2p_port = config.GetP2PPort();

    // Override with network defaults if using default values
    if (n_rest_port == REST_API_PORT) {
        n_rest_port = network_params.default_rest_port;
    }
    if (n_p2p_port == P2P_PORT) {
        n_p2p_port = network_params.default_p2p_port;
    }

    std::string str_log_dir = config.GetLogDir();
    std::string str_log_level = config.GetLogLevel();
    int n_log_file_size_mb = config.GetLogFileSizeMB();
    int n_log_file_keep = config.GetLogFileKeep();

    // Use network-specific data directory
    std::string str_data_dir = config.GetNetworkDataDir(str_configured_network);

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

    // Convert log directory to absolute path (needed for daemon/service mode)
#ifdef _WIN32
    // Windows: Check if path starts with drive letter (e.g., "C:\") or is UNC path
    bool is_absolute = (str_log_dir.length() >= 2 && str_log_dir[1] == ':') ||
                       (str_log_dir.length() >= 2 && str_log_dir[0] == '\\' && str_log_dir[1] == '\\');
#else
    // POSIX: Check if path starts with '/'
    bool is_absolute = (str_log_dir.length() > 0 && str_log_dir[0] == '/');
#endif
    if (!is_absolute) {
        // Relative path - convert to absolute
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
#ifdef _WIN32
            str_log_dir = std::string(cwd) + "\\" + str_log_dir;
#else
            str_log_dir = std::string(cwd) + "/" + str_log_dir;
#endif
        }
    }

    // Convert data directory to absolute path (needed for daemon/service mode)
#ifdef _WIN32
    // Windows: Check if path starts with drive letter (e.g., "C:\") or is UNC path
    bool is_data_absolute = (str_data_dir.length() >= 2 && str_data_dir[1] == ':') ||
                            (str_data_dir.length() >= 2 && str_data_dir[0] == '\\' && str_data_dir[1] == '\\');
#else
    // POSIX: Check if path starts with '/'
    bool is_data_absolute = (str_data_dir.length() > 0 && str_data_dir[0] == '/');
#endif
    if (!is_data_absolute) {
        // Relative path - convert to absolute
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
#ifdef _WIN32
            str_data_dir = std::string(cwd) + "\\" + str_data_dir;
#else
            str_data_dir = std::string(cwd) + "/" + str_data_dir;
#endif
            config.SetValue("data_dir", str_data_dir);
        }
    }

    // Setup signal handlers (POSIX) or console control handler (Windows console mode)
    CDaemon::SetupSignalHandlers();

    // Daemonize if requested (POSIX only)
    if (config.IsDaemonMode()) {
        // Note: Using cout here because logger is not yet initialized
        std::cout << "[Main] Starting in daemon mode...\n";
        std::cout << "[Main] Log directory: " << str_log_dir << "\n";
#ifdef _WIN32
        std::cerr << "[Main] Warning: Windows daemon mode (-d) runs as console background process\n";
        std::cerr << "[Main] For true Windows service, use: bweave.exe --install-service\n";
#endif
        if (!CDaemon::Daemonize("/tmp/bweave.pid")) {
            std::cerr << "Failed to daemonize process\n";
            return 1;
        }
        // After daemonization, file descriptors are closed and working directory is /
        // So we must initialize logger after daemonization with absolute path
    }

    // Initialize logger (after daemonization to avoid file descriptor issues)
    ELogLevel log_level = ParseLogLevelString(str_log_level);
    if (!InitializeLogger(str_log_dir, log_level, n_log_file_size_mb, n_log_file_keep)) {
        // In daemon mode, stderr is redirected to /dev/null, so this won't be seen
        // But in non-daemon mode, user will see the error
        std::cerr << "Error: Failed to initialize logger\n";
        return 1;
    }
    LOG_INFO("=== Blockweave Daemon Starting ===");
    if (config.IsDaemonMode()) {
        LOG_INFO("Daemon process started successfully");
    }
    LOG_INFO("Log level set to: " + str_log_level);

    LOG_INFO("=== Blockweave Daemon ===");
    LOG_INFO("Network: " + network_params.network_name + " (magic: 0x" +
             std::to_string(network_params.magic_bytes) + ")");
    LOG_INFO("Miner address: " + str_miner_address + "...");
    LOG_INFO("REST API port: " + std::to_string(n_rest_port));
    LOG_INFO("P2P port: " + std::to_string(n_p2p_port));
    // LOG_INFO("Target block time: " + std::to_string(network_params.target_block_time_seconds) + " seconds");
    if (network_params.fast_mining) {
        LOG_INFO("Fast mining mode: ENABLED (localnet only)");
    }
    LOG_INFO("REST worker threads: " + std::to_string(REST_WORKER_THREADS));
    LOG_INFO("Data directory: " + str_data_dir);

    // Note: Data directory is automatically created by config.GetNetworkDataDir()
    // Create blockweave with data/blocks subdirectory for block storage
    std::string str_blocks_dir = str_data_dir + "/blocks";
    auto p_weave = std::make_shared<CBlockweave>(str_blocks_dir);
    LOG_INFO("Blockweave instance created with block storage at: " + str_blocks_dir);

    // Create peer manager (before REST API so we can pass it to REST API)
    int n_max_outbound = config.GetMaxOutboundPeers();
    int n_max_inbound = config.GetMaxInboundPeers();
    int n_peers_ping_time = config.GetPeersPingTime();
    uint32_t n_network_magic = network_params.magic_bytes;
    LOG_INFO("Creating peer manager on port " + std::to_string(n_p2p_port));
    LOG_INFO("Peers PING time: " + std::to_string(n_peers_ping_time) + " seconds");
    CPeerManager peer_manager(n_p2p_port, n_max_outbound, n_max_inbound, MAX_INBOUND_WORKER_THREADS, n_peers_ping_time, n_network_magic);

    // Start REST API server (1 listener thread + N worker threads)
    LOG_INFO("Starting REST API server on port " + std::to_string(n_rest_port));
    CRestApiServer rest_api(p_weave.get(), &peer_manager, &config, str_miner_address, n_rest_port);
    if (!rest_api.Start()) {
        LOG_ERROR("Failed to start REST API server on port " + std::to_string(n_rest_port));
        return 1;
    }
    LOG_INFO("REST API server started successfully");

    // Start peer manager
    LOG_INFO("Starting peer manager on port " + std::to_string(n_p2p_port));
    if (!peer_manager.Start()) {
        LOG_ERROR("Failed to start peer manager on port " + std::to_string(n_p2p_port));
        rest_api.Stop();
        return 1;
    }
    LOG_INFO("Peer manager started successfully");

    // Connect peer manager to blockweave for transaction broadcasting
    p_weave->SetPeerManager(&peer_manager);

    // Connect blockweave to peer manager for blockchain queries
    peer_manager.SetBlockweave(p_weave);

    // Start mining manager
    LOG_INFO("Starting mining manager");
    CMiningManager mining_manager(p_weave.get(), str_miner_address, network_type);
    p_weave->StartMining();
    if (!mining_manager.Start()) {
        LOG_ERROR("Failed to start mining manager");
        peer_manager.Stop();
        rest_api.Stop();
        return 1;
    }
    LOG_INFO("Mining manager started successfully");

#ifdef _WIN32
    // Report SERVICE_RUNNING status to SCM (Windows service mode only)
    if (IsRunningAsService()) {
        SetServiceStatusWrapper(SERVICE_RUNNING, NO_ERROR, 0);
        LOG_INFO("Windows service status: RUNNING");
    }
#endif

    LOG_INFO("bweave daemon is running and ready to accept requests");
    LOG_INFO("Status: Starting main loop");

    // Main loop - wait for shutdown signal
    while (!g_f_shutdown_requested) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    LOG_INFO("Status: Stopping - exited main loop");

    LOG_INFO("Shutdown signal received. Cleaning up...");
    LOG_INFO("Shutdown signal received, initiating graceful shutdown");

#ifdef _WIN32
    // Report SERVICE_STOP_PENDING to SCM (Windows service mode only)
    if (IsRunningAsService()) {
        SetServiceStatusWrapper(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        LOG_INFO("Windows service status: STOP_PENDING");
    }
#endif

    // Stop mining manager
    mining_manager.Stop();

    // Stop peer manager
    peer_manager.Stop();

    // Stop REST API server
    rest_api.Stop();

    // Print final state
    p_weave->PrintChain();

    // Cleanup PID file if in daemon mode
    if (config.IsDaemonMode()) {
        CDaemon::RemovePidFile("/tmp/bweave.pid");
        LOG_INFO("PID file removed");
    }

    LOG_INFO("Shutdown complete");

    // Flush logger before exit
    if (g_p_logger) {
        g_p_logger->Flush();
    }

    return 0;
}

