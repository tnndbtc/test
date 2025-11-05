// ============= bweave_cli.cpp =============
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <cstring>

// Platform-specific includes
#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #ifndef PATH_MAX
        #define PATH_MAX MAX_PATH
    #endif
    #define sleep(n) Sleep((n) * 1000)
#else
    #include <unistd.h>
    #include <signal.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <libgen.h>
    #include <limits.h>
#endif

#ifdef _WIN32
const std::string STR_PID_FILE = "bweave.pid";  // Windows: current directory
const std::string STR_DAEMON_EXECUTABLE = "bweave.exe";
#else
const std::string STR_PID_FILE = "/tmp/bweave.pid";
const std::string STR_DAEMON_EXECUTABLE = "bweave";
#endif

// Simple PID file removal (no dependencies)
void RemovePidFile(const std::string& str_pid_file) {
#ifdef _WIN32
    DeleteFileA(str_pid_file.c_str());
#else
    unlink(str_pid_file.c_str());
#endif
}

// Find bweave executable
std::string FindRestDaemon(const char* argv0) {
    // Try multiple locations
    std::vector<std::string> paths = {
#ifdef _WIN32
        ".\\bweave.exe",                // Current directory
        ".\\build\\bweave.exe",         // Build directory
#else
        "./bweave",                     // Current directory
        "./build/bweave",               // Build directory
#endif
    };

    // Try to get executable directory from argv[0]
    if (argv0 != nullptr) {
        char resolved[PATH_MAX];
#ifdef _WIN32
        if (GetFullPathNameA(argv0, PATH_MAX, resolved, NULL) != 0) {
            // Extract directory
            std::string str_path(resolved);
            size_t pos = str_path.find_last_of("\\/");
            if (pos != std::string::npos) {
                std::string dir = str_path.substr(0, pos);
                paths.insert(paths.begin(), dir + "\\bweave.exe");
            }
        }
#else
        if (realpath(argv0, resolved)) {
            char* dir = dirname(resolved);
            paths.insert(paths.begin(), std::string(dir) + "/bweave");
        }
#endif
    }

#ifdef _WIN32
    // Try to get executable directory from GetModuleFileName
    char exe_path[MAX_PATH];
    if (GetModuleFileNameA(NULL, exe_path, MAX_PATH) != 0) {
        std::string str_path(exe_path);
        size_t pos = str_path.find_last_of("\\/");
        if (pos != std::string::npos) {
            std::string dir = str_path.substr(0, pos);
            paths.insert(paths.begin(), dir + "\\bweave.exe");
        }
    }
#else
    // Try /proc/self/exe method (Linux)
    char self_path[PATH_MAX];
    ssize_t n_len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
    if (n_len > 0) {
        self_path[n_len] = '\0';
        char* dir = dirname(self_path);
        paths.insert(paths.begin(), std::string(dir) + "/bweave");
    }
#endif

    // Check which path exists
    for (const auto& path : paths) {
#ifdef _WIN32
        DWORD attrs = GetFileAttributesA(path.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            // File exists - convert to absolute path
            char abs_path[MAX_PATH];
            if (GetFullPathNameA(path.c_str(), MAX_PATH, abs_path, NULL) != 0) {
                return std::string(abs_path);
            }
            return path;
        }
#else
        if (access(path.c_str(), X_OK) == 0) {
            // Convert to absolute path
            char abs_path[PATH_MAX];
            if (realpath(path.c_str(), abs_path)) {
                return std::string(abs_path);
            }
            return path;
        }
#endif
    }

    return "";
}

void PrintUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <command> [options]\n\n";
    std::cout << "Commands:\n";
#ifdef _WIN32
    std::cout << "  install                Install Windows service\n";
    std::cout << "  uninstall              Uninstall Windows service\n";
    std::cout << "  start [-c <config>]    Start the blockweave daemon/service\n";
    std::cout << "  stop                   Stop the blockweave daemon/service\n";
    std::cout << "  status                 Check daemon/service status\n";
    std::cout << "  restart [-c <config>]  Restart the blockweave daemon/service\n";
#else
    std::cout << "  start [-c <config>]    Start the blockweave daemon\n";
    std::cout << "  stop                   Stop the blockweave daemon\n";
    std::cout << "  status                 Check daemon status\n";
    std::cout << "  restart [-c <config>]  Restart the blockweave daemon\n";
#endif
    std::cout << "\nOptions:\n";
    std::cout << "  -c, --config <file>    Configuration file (default: bweave.conf)\n";
    std::cout << "\nExamples:\n";
#ifdef _WIN32
    std::cout << "  " << program_name << " install\n";
    std::cout << "  " << program_name << " start\n";
    std::cout << "  " << program_name << " stop\n";
    std::cout << "  " << program_name << " status\n";
    std::cout << "  " << program_name << " uninstall\n";
#else
    std::cout << "  " << program_name << " start\n";
    std::cout << "  " << program_name << " start -c custom.conf\n";
    std::cout << "  " << program_name << " stop\n";
    std::cout << "  " << program_name << " status\n";
#endif
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

#ifdef _WIN32
// Windows: Check if service is installed
bool IsServiceInstalled() {
    SC_HANDLE h_sc_manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (h_sc_manager == NULL) {
        return false;
    }

    SC_HANDLE h_service = OpenServiceA(h_sc_manager, "bweave", SERVICE_QUERY_STATUS);
    bool f_installed = (h_service != NULL);

    if (h_service) CloseServiceHandle(h_service);
    CloseServiceHandle(h_sc_manager);

    return f_installed;
}

// Windows: Check if service is running
bool IsServiceRunning() {
    SC_HANDLE h_sc_manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (h_sc_manager == NULL) {
        return false;
    }

    SC_HANDLE h_service = OpenServiceA(h_sc_manager, "bweave", SERVICE_QUERY_STATUS);
    if (h_service == NULL) {
        CloseServiceHandle(h_sc_manager);
        return false;
    }

    SERVICE_STATUS status;
    bool f_running = false;
    if (QueryServiceStatus(h_service, &status)) {
        f_running = (status.dwCurrentState == SERVICE_RUNNING);
    }

    CloseServiceHandle(h_service);
    CloseServiceHandle(h_sc_manager);

    return f_running;
}

// Windows: Check if console daemon is running (PID file method)
bool IsDaemonRunning() {
    int n_pid;
    if (!ReadPidFile(n_pid)) {
        return false;
    }

    // Check if process exists
    HANDLE h_process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, n_pid);
    if (h_process != NULL) {
        CloseHandle(h_process);
        return true;
    }

    // Stale PID file
    RemovePidFile(STR_PID_FILE);
    return false;
}

#else
// POSIX: Check if daemon is running
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
#endif

int StartDaemon(const std::string& str_config_file, const char* argv0) {
#ifdef _WIN32
    // Windows: Check if service is installed first
    if (IsServiceInstalled()) {
        // Use service control manager
        std::cout << "[CLI] Starting bweave service...\n";

        SC_HANDLE h_sc_manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
        if (h_sc_manager == NULL) {
            std::cerr << "[CLI] Failed to open Service Control Manager\n";
            std::cerr << "[CLI] Error code: " << GetLastError() << "\n";
            return 1;
        }

        SC_HANDLE h_service = OpenServiceA(h_sc_manager, "bweave", SERVICE_START | SERVICE_QUERY_STATUS);
        if (h_service == NULL) {
            std::cerr << "[CLI] Failed to open service\n";
            std::cerr << "[CLI] Error code: " << GetLastError() << "\n";
            CloseServiceHandle(h_sc_manager);
            return 1;
        }

        // Check if already running
        SERVICE_STATUS status;
        if (QueryServiceStatus(h_service, &status)) {
            if (status.dwCurrentState == SERVICE_RUNNING) {
                std::cout << "[CLI] Service is already running\n";
                CloseServiceHandle(h_service);
                CloseServiceHandle(h_sc_manager);
                return 0;
            }
        }

        // Start service
        if (!StartServiceA(h_service, 0, NULL)) {
            DWORD error = GetLastError();
            if (error == ERROR_SERVICE_ALREADY_RUNNING) {
                std::cout << "[CLI] Service is already running\n";
            } else {
                std::cerr << "[CLI] Failed to start service\n";
                std::cerr << "[CLI] Error code: " << error << "\n";
                CloseServiceHandle(h_service);
                CloseServiceHandle(h_sc_manager);
                return 1;
            }
        }

        // Wait for service to start
        std::cout << "[CLI] Waiting for service to start...\n";
        for (int i = 0; i < 10; i++) {
            Sleep(500);
            if (QueryServiceStatus(h_service, &status)) {
                if (status.dwCurrentState == SERVICE_RUNNING) {
                    std::cout << "[CLI] Service started successfully\n";
                    CloseServiceHandle(h_service);
                    CloseServiceHandle(h_sc_manager);
                    return 0;
                }
            }
        }

        std::cerr << "[CLI] Service did not start within timeout\n";
        CloseServiceHandle(h_service);
        CloseServiceHandle(h_sc_manager);
        return 1;
    } else {
        // Not installed as service - run as console daemon
        std::cout << "[CLI] Service not installed, starting as console daemon...\n";

        // Check if already running
        if (IsDaemonRunning()) {
            std::cout << "[CLI] Daemon is already running\n";
            int n_pid;
            if (ReadPidFile(n_pid)) {
                std::cout << "[CLI] PID: " << n_pid << "\n";
            }
            return 1;
        }

        // Find bweave executable
        std::string str_daemon_path = FindRestDaemon(argv0);
        if (str_daemon_path.empty()) {
            std::cerr << "[CLI] Error: Cannot find bweave.exe\n";
            std::cerr << "[CLI] Searched locations:\n";
            std::cerr << "[CLI]   - Same directory as bweave_cli.exe\n";
            std::cerr << "[CLI]   - .\\bweave.exe\n";
            std::cerr << "[CLI]   - .\\build\\bweave.exe\n";
            return 1;
        }

        std::cout << "[CLI] Found bweave at: " << str_daemon_path << "\n";
        std::cout << "[CLI] Starting blockweave daemon...\n";

        // Build command line
        std::string cmd_line = "\"" + str_daemon_path + "\" -d";
        if (!str_config_file.empty()) {
            cmd_line += " -c \"" + str_config_file + "\"";
        }

        // Create process with CREATE_NO_WINDOW flag (background process)
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        if (!CreateProcessA(
                NULL,
                const_cast<char*>(cmd_line.c_str()),
                NULL,
                NULL,
                FALSE,
                CREATE_NO_WINDOW | DETACHED_PROCESS,
                NULL,
                NULL,
                &si,
                &pi)) {
            std::cerr << "[CLI] Failed to start daemon\n";
            std::cerr << "[CLI] Error code: " << GetLastError() << "\n";
            return 1;
        }

        // Wait a moment for daemon to initialize
        std::cout << "[CLI] Waiting for daemon to initialize...\n";
        for (int n_i = 0; n_i < 10; n_i++) {
            Sleep(500);

            if (IsDaemonRunning()) {
                int n_pid;
                if (ReadPidFile(n_pid)) {
                    std::cout << "[CLI] Daemon started successfully (PID: " << n_pid << ")\n";
                } else {
                    std::cout << "[CLI] Daemon started successfully\n";
                }
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                return 0;
            }
        }

        // Check if process exited with error
        DWORD exit_code;
        if (GetExitCodeProcess(pi.hProcess, &exit_code) && exit_code != STILL_ACTIVE) {
            std::cerr << "[CLI] Daemon process exited with code " << exit_code << "\n";
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return 1;
        }

        std::cerr << "[CLI] Failed to start daemon - PID file not created within timeout\n";
        std::cerr << "[CLI] Check log files for errors\n";

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 1;
    }
#else
    // POSIX: Original fork/exec implementation
    // Check if already running
    if (IsDaemonRunning()) {
        std::cout << "[CLI] Daemon is already running\n";
        int n_pid;
        if (ReadPidFile(n_pid)) {
            std::cout << "[CLI] PID: " << n_pid << "\n";
        }
        return 1;
    }

    // Find bweave executable
    std::string str_daemon_path = FindRestDaemon(argv0);
    if (str_daemon_path.empty()) {
        std::cerr << "[CLI] Error: Cannot find bweave executable\n";
        std::cerr << "[CLI] Searched locations:\n";
        std::cerr << "[CLI]   - Same directory as bweave_cli\n";
        std::cerr << "[CLI]   - ./bweave\n";
        std::cerr << "[CLI]   - ./build/bweave\n";
        return 1;
    }

    std::cout << "[CLI] Found bweave at: " << str_daemon_path << "\n";
    std::cout << "[CLI] Starting blockweave daemon...\n";

    // Fork to start daemon
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[CLI] Failed to fork: " << strerror(errno) << "\n";
        return 1;
    }

    if (pid == 0) {
        // Child process - exec bweave
        if (str_config_file.empty()) {
            execl(str_daemon_path.c_str(), "bweave", "-d", nullptr);
        } else {
            execl(str_daemon_path.c_str(), "bweave", "-d", "-c", str_config_file.c_str(), nullptr);
        }

        // If exec fails
        std::cerr << "[CLI] Failed to execute bweave: " << strerror(errno) << "\n";
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
    std::cerr << "[CLI] Check log files for errors (default location: ./log/bweave.log)\n";

    return 1;
#endif
}

int StopDaemon() {
#ifdef _WIN32
    // Windows: Check if service is installed first
    if (IsServiceInstalled()) {
        // Use service control manager
        std::cout << "[CLI] Stopping bweave service...\n";

        SC_HANDLE h_sc_manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
        if (h_sc_manager == NULL) {
            std::cerr << "[CLI] Failed to open Service Control Manager\n";
            return 1;
        }

        SC_HANDLE h_service = OpenServiceA(h_sc_manager, "bweave", SERVICE_STOP | SERVICE_QUERY_STATUS);
        if (h_service == NULL) {
            std::cerr << "[CLI] Failed to open service\n";
            CloseServiceHandle(h_sc_manager);
            return 1;
        }

        // Check if running
        SERVICE_STATUS status;
        if (QueryServiceStatus(h_service, &status)) {
            if (status.dwCurrentState == SERVICE_STOPPED) {
                std::cout << "[CLI] Service is not running\n";
                CloseServiceHandle(h_service);
                CloseServiceHandle(h_sc_manager);
                return 0;
            }
        }

        // Stop service
        if (!ControlService(h_service, SERVICE_CONTROL_STOP, &status)) {
            std::cerr << "[CLI] Failed to stop service\n";
            std::cerr << "[CLI] Error code: " << GetLastError() << "\n";
            CloseServiceHandle(h_service);
            CloseServiceHandle(h_sc_manager);
            return 1;
        }

        // Wait for service to stop
        for (int i = 0; i < 10; i++) {
            Sleep(1000);
            if (QueryServiceStatus(h_service, &status)) {
                if (status.dwCurrentState == SERVICE_STOPPED) {
                    std::cout << "[CLI] Service stopped successfully\n";
                    CloseServiceHandle(h_service);
                    CloseServiceHandle(h_sc_manager);
                    return 0;
                }
            }
        }

        std::cerr << "[CLI] Service did not stop within timeout\n";
        CloseServiceHandle(h_service);
        CloseServiceHandle(h_sc_manager);
        return 1;
    } else {
        // Console daemon mode
        if (!IsDaemonRunning()) {
            std::cout << "[CLI] Daemon is not running\n";
            return 0;
        }

        int n_pid;
        if (!ReadPidFile(n_pid)) {
            std::cerr << "[CLI] Failed to read PID file\n";
            return 1;
        }

        std::cout << "[CLI] Stopping blockweave daemon (PID: " << n_pid << ")...\n";

        // Try to send console Ctrl+C event (only works if same console)
        // Otherwise, terminate the process
        HANDLE h_process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, n_pid);
        if (h_process == NULL) {
            std::cerr << "[CLI] Failed to open process: " << GetLastError() << "\n";
            return 1;
        }

        // Terminate process (not graceful but works)
        if (!TerminateProcess(h_process, 0)) {
            std::cerr << "[CLI] Failed to terminate process: " << GetLastError() << "\n";
            CloseHandle(h_process);
            return 1;
        }

        // Wait for process to exit
        DWORD result = WaitForSingleObject(h_process, 10000);  // 10 second timeout
        CloseHandle(h_process);

        if (result == WAIT_OBJECT_0) {
            RemovePidFile(STR_PID_FILE);
            std::cout << "[CLI] Daemon stopped successfully\n";
            return 0;
        } else {
            std::cerr << "[CLI] Daemon did not stop within timeout\n";
            return 1;
        }
    }
#else
    // POSIX: Send SIGTERM
    if (!IsDaemonRunning()) {
        std::cout << "[CLI] Daemon is not running\n";
        return 0;
    }

    int n_pid;
    if (!ReadPidFile(n_pid)) {
        std::cerr << "[CLI] Failed to read PID file\n";
        return 1;
    }

    std::cout << "[CLI] Stopping blockweave daemon (PID: " << n_pid << ")...\n";

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
#endif
}

int ShowStatus() {
#ifdef _WIN32
    // Windows: Check service first
    if (IsServiceInstalled()) {
        if (IsServiceRunning()) {
            std::cout << "[CLI] Service is running\n";
        } else {
            std::cout << "[CLI] Service is installed but not running\n";
        }
        return 0;
    } else {
        // Console daemon
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
#else
    // POSIX
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
#endif
}

int RestartDaemon(const std::string& str_config_file, const char* argv0) {
    std::cout << "[CLI] Restarting daemon...\n";

#ifdef _WIN32
    if (IsServiceInstalled() && IsServiceRunning()) {
#else
    if (IsDaemonRunning()) {
#endif
        std::cout << "[CLI] Stopping current daemon...\n";
        if (StopDaemon() != 0) {
            std::cerr << "[CLI] Failed to stop daemon\n";
            return 1;
        }
        sleep(1);
    }

    return StartDaemon(str_config_file, argv0);
}

#ifdef _WIN32
int InstallService(const char* argv0) {
    // Find bweave executable
    std::string str_daemon_path = FindRestDaemon(argv0);
    if (str_daemon_path.empty()) {
        std::cerr << "[CLI] Error: Cannot find bweave.exe\n";
        return 1;
    }

    // Call bweave.exe --install-service
    std::string cmd_line = "\"" + str_daemon_path + "\" --install-service";

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(
            NULL,
            const_cast<char*>(cmd_line.c_str()),
            NULL,
            NULL,
            FALSE,
            0,
            NULL,
            NULL,
            &si,
            &pi)) {
        std::cerr << "[CLI] Failed to run bweave.exe --install-service\n";
        return 1;
    }

    // Wait for installation to complete
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return exit_code;
}

int UninstallService(const char* argv0) {
    // Find bweave executable
    std::string str_daemon_path = FindRestDaemon(argv0);
    if (str_daemon_path.empty()) {
        std::cerr << "[CLI] Error: Cannot find bweave.exe\n";
        return 1;
    }

    // Call bweave.exe --uninstall-service
    std::string cmd_line = "\"" + str_daemon_path + "\" --uninstall-service";

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(
            NULL,
            const_cast<char*>(cmd_line.c_str()),
            NULL,
            NULL,
            FALSE,
            0,
            NULL,
            NULL,
            &si,
            &pi)) {
        std::cerr << "[CLI] Failed to run bweave.exe --uninstall-service\n";
        return 1;
    }

    // Wait for uninstallation to complete
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return exit_code;
}
#endif

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
#ifdef _WIN32
    else if (str_command == "install") {
        return InstallService(argv[0]);
    }
    else if (str_command == "uninstall") {
        return UninstallService(argv[0]);
    }
#endif
    else {
        std::cerr << "Unknown command: " << str_command << "\n\n";
        PrintUsage(argv[0]);
        return 1;
    }

    return 0;
}
