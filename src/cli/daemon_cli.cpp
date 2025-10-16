// ============= daemon_cli.cpp =============
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cstring>
#include <libgen.h>
#include <limits.h>

const std::string STR_PID_FILE = "/tmp/rest_daemon.pid";
const std::string STR_DAEMON_EXECUTABLE = "rest_daemon";

// Simple PID file removal (no dependencies)
void RemovePidFile(const std::string& str_pid_file) {
    unlink(str_pid_file.c_str());
}

// Get the directory where this executable is located
std::string GetExecutableDirectory() {
    char path[PATH_MAX];
    ssize_t n_len = readlink("/proc/self/exe", path, sizeof(path) - 1);

    // On macOS, /proc/self/exe doesn't exist, try different approach
    if (n_len < 0) {
        // Try to use realpath on argv[0] - we'll pass this as parameter
        return "";
    }

    path[n_len] = '\0';
    char* dir = dirname(path);
    return std::string(dir);
}

// Find rest_daemon executable
std::string FindRestDaemon(const char* argv0) {
    // Try multiple locations
    std::vector<std::string> paths = {
        "./rest_daemon",                    // Current directory
        "./build/rest_daemon",              // Build directory
    };

    // Try to get executable directory from argv[0]
    if (argv0 != nullptr) {
        char resolved[PATH_MAX];
        if (realpath(argv0, resolved)) {
            char* dir = dirname(resolved);
            paths.insert(paths.begin(), std::string(dir) + "/rest_daemon");
        }
    }

    // Try /proc/self/exe method (Linux)
    char self_path[PATH_MAX];
    ssize_t n_len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (n_len > 0) {
        self_path[n_len] = '\0';
        char* dir = dirname(self_path);
        paths.insert(paths.begin(), std::string(dir) + "/rest_daemon");
    }

    // Check which path exists
    for (const auto& path : paths) {
        if (access(path.c_str(), X_OK) == 0) {
            // Convert to absolute path
            char abs_path[PATH_MAX];
            if (realpath(path.c_str(), abs_path)) {
                return std::string(abs_path);
            }
            return path;
        }
    }

    return "";
}

void PrintUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <command> [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  start [-c <config>]    Start the REST daemon\n";
    std::cout << "  stop                   Stop the REST daemon\n";
    std::cout << "  status                 Check daemon status\n";
    std::cout << "  restart [-c <config>]  Restart the REST daemon\n";
    std::cout << "\nOptions:\n";
    std::cout << "  -c, --config <file>    Configuration file (default: blockweave.conf)\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << program_name << " start\n";
    std::cout << "  " << program_name << " start -c custom.conf\n";
    std::cout << "  " << program_name << " stop\n";
    std::cout << "  " << program_name << " status\n";
}

bool ReadPidFile(int& n_pid) {
    std::ifstream file(STR_PID_FILE);
    if (!file.is_open()) {
        return false;
    }
    file >> n_pid;
    file.close();
    return true;
}

bool IsDaemonRunning() {
    int n_pid;
    if (!ReadPidFile(n_pid)) {
        return false;
    }

    // Check if process exists
    if (kill(n_pid, 0) == 0) {
        return true;
    }

    // Stale PID file
    RemovePidFile(STR_PID_FILE);
    return false;
}

int StartDaemon(const std::string& str_config_file, const char* argv0) {
    // Check if already running
    if (IsDaemonRunning()) {
        std::cout << "[CLI] Daemon is already running\n";
        int n_pid;
        if (ReadPidFile(n_pid)) {
            std::cout << "[CLI] PID: " << n_pid << "\n";
        }
        return 1;
    }

    // Find rest_daemon executable
    std::string str_daemon_path = FindRestDaemon(argv0);
    if (str_daemon_path.empty()) {
        std::cerr << "[CLI] Error: Cannot find rest_daemon executable\n";
        std::cerr << "[CLI] Searched locations:\n";
        std::cerr << "[CLI]   - Same directory as daemon_cli\n";
        std::cerr << "[CLI]   - ./rest_daemon\n";
        std::cerr << "[CLI]   - ./build/rest_daemon\n";
        return 1;
    }

    std::cout << "[CLI] Found rest_daemon at: " << str_daemon_path << "\n";
    std::cout << "[CLI] Starting REST daemon...\n";

    // Fork to start daemon
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[CLI] Failed to fork: " << strerror(errno) << "\n";
        return 1;
    }

    if (pid == 0) {
        // Child process - exec rest_daemon
        if (str_config_file.empty()) {
            execl(str_daemon_path.c_str(), "rest_daemon", "-d", nullptr);
        } else {
            execl(str_daemon_path.c_str(), "rest_daemon", "-d", "-c", str_config_file.c_str(), nullptr);
        }

        // If exec fails
        std::cerr << "[CLI] Failed to execute rest_daemon: " << strerror(errno) << "\n";
        exit(1);
    }

    // Parent process - wait for child to complete exec
    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result > 0) {
        // Child process exited immediately - exec failed
        std::cerr << "[CLI] Failed to start daemon - process exited immediately\n";
        return 1;
    }

    // Wait for daemon to complete daemonization and write PID file
    // The double-fork process takes time, so retry for up to 5 seconds
    std::cout << "[CLI] Waiting for daemon to initialize...\n";
    for (int n_i = 0; n_i < 10; n_i++) {
        usleep(500000);  // 500ms

        if (IsDaemonRunning()) {
            int n_pid;
            if (ReadPidFile(n_pid)) {
                std::cout << "[CLI] Daemon started successfully (PID: " << n_pid << ")\n";
            } else {
                std::cout << "[CLI] Daemon started successfully\n";
            }
            return 0;
        }
    }

    // Check one more time if the child process failed
    result = waitpid(pid, &status, WNOHANG);
    if (result > 0 && WIFEXITED(status)) {
        std::cerr << "[CLI] Failed to start daemon - process exited with code "
                  << WEXITSTATUS(status) << "\n";
        return 1;
    }

    std::cerr << "[CLI] Failed to start daemon - PID file not created within timeout\n";
    std::cerr << "[CLI] Check log files for errors (default location: ./logs/rest_daemon_*.log)\n";

    return 1;
}

int StopDaemon() {
    if (!IsDaemonRunning()) {
        std::cout << "[CLI] Daemon is not running\n";
        return 0;
    }

    int n_pid;
    if (!ReadPidFile(n_pid)) {
        std::cerr << "[CLI] Failed to read PID file\n";
        return 1;
    }

    std::cout << "[CLI] Stopping REST daemon (PID: " << n_pid << ")...\n";

    // Send SIGTERM for graceful shutdown
    if (kill(n_pid, SIGTERM) < 0) {
        std::cerr << "[CLI] Failed to send signal: " << strerror(errno) << "\n";
        return 1;
    }

    // Wait for daemon to stop (max 10 seconds)
    for (int n_i = 0; n_i < 10; n_i++) {
        sleep(1);
        if (!IsDaemonRunning()) {
            std::cout << "[CLI] Daemon stopped successfully\n";
            return 0;
        }
    }

    std::cerr << "[CLI] Daemon did not stop within timeout\n";
    std::cerr << "[CLI] You may need to force kill with: kill -9 " << n_pid << "\n";
    return 1;
}

int ShowStatus() {
    if (IsDaemonRunning()) {
        int n_pid;
        if (ReadPidFile(n_pid)) {
            std::cout << "[CLI] Daemon is running (PID: " << n_pid << ")\n";
        } else {
            std::cout << "[CLI] Daemon is running\n";
        }
        return 0;
    } else {
        std::cout << "[CLI] Daemon is not running\n";
        return 1;
    }
}

int RestartDaemon(const std::string& str_config_file, const char* argv0) {
    std::cout << "[CLI] Restarting daemon...\n";

    if (IsDaemonRunning()) {
        std::cout << "[CLI] Stopping current daemon...\n";
        if (StopDaemon() != 0) {
            std::cerr << "[CLI] Failed to stop daemon\n";
            return 1;
        }
        sleep(1);
    }

    return StartDaemon(str_config_file, argv0);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::string str_command = argv[1];
    std::string str_config_file = "";

    // Parse additional options
    for (int n_i = 2; n_i < argc; n_i++) {
        std::string str_arg = argv[n_i];
        if ((str_arg == "-c" || str_arg == "--config") && n_i + 1 < argc) {
            str_config_file = argv[++n_i];
        }
    }

    if (str_command == "start") {
        return StartDaemon(str_config_file, argv[0]);
    }
    else if (str_command == "stop") {
        return StopDaemon();
    }
    else if (str_command == "status") {
        return ShowStatus();
    }
    else if (str_command == "restart") {
        return RestartDaemon(str_config_file, argv[0]);
    }
    else {
        std::cerr << "Unknown command: " << str_command << "\n\n";
        PrintUsage(argv[0]);
        return 1;
    }

    return 0;
}
