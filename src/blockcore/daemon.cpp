// ============= daemon.cpp =============
#include "daemon.h"
#include "logger/logger.h"
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>

#ifndef _WIN32
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <signal.h>
    #include <fcntl.h>
#else
    #include <windows.h>
    #include <process.h>
#endif

// Global shutdown flag
#ifndef _WIN32
volatile sig_atomic_t g_f_shutdown_requested = 0;
#else
volatile int g_f_shutdown_requested = 0;
#endif

// Signal handler (POSIX only)
#ifndef _WIN32
static void SignalHandler(int n_signal) {
    if (n_signal == SIGTERM || n_signal == SIGINT) {
        g_f_shutdown_requested = 1;
    }
}
#endif

bool CDaemon::Daemonize(const std::string& str_pid_file) {
#ifdef _WIN32
    // Windows: Daemonization not supported (use Windows Service instead)
    // PID files are not needed on Windows - Services are managed by SCM
    (void)str_pid_file; // Suppress unused parameter warning
    std::cerr << "[Daemon] Note: Running as foreground process on Windows\n";
    std::cerr << "[Daemon] To run as background service, install as Windows Service using 'sc create' or 'bweave_cli service install'\n";

    return true;
#else
    // POSIX: Traditional daemonization
    // Fork first child
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[Daemon] Failed to fork: " << strerror(errno) << "\n";
        return false;
    }

    // Parent exits
    if (pid > 0) {
        exit(0);
    }

    // Create new session
    if (setsid() < 0) {
        std::cerr << "[Daemon] Failed to create new session: " << strerror(errno) << "\n";
        return false;
    }

    // Fork second child
    pid = fork();
    if (pid < 0) {
        std::cerr << "[Daemon] Failed to fork second time: " << strerror(errno) << "\n";
        return false;
    }

    // Parent exits
    if (pid > 0) {
        exit(0);
    }

    // Set file permissions
    umask(0);

    // Change working directory to root
    if (chdir("/") < 0) {
        std::cerr << "[Daemon] Failed to change directory to /: " << strerror(errno) << "\n";
        return false;
    }

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect to /dev/null
    int n_fd = open("/dev/null", O_RDWR);
    if (n_fd >= 0) {
        dup2(n_fd, STDIN_FILENO);
        dup2(n_fd, STDOUT_FILENO);
        dup2(n_fd, STDERR_FILENO);
        if (n_fd > STDERR_FILENO) {
            close(n_fd);
        }
    }

    // Write PID file
    if (!WritePidFile(str_pid_file)) {
        return false;
    }

    std::cout << "[Daemon] Process daemonized successfully\n";
    return true;
#endif
}

bool CDaemon::WritePidFile(const std::string& str_pid_file) {
#ifdef _WIN32
    // Windows: Do not create PID files - Services are managed by SCM
    (void)str_pid_file; // Suppress unused parameter warning
    LOG_INFO("PID files not used on Windows - use Windows Service instead");
    return true;
#else
    // POSIX: Write PID file
    std::ofstream file(str_pid_file);
    if (!file.is_open()) {
        std::cerr << "[Daemon] Failed to write PID file: " << str_pid_file << "\n";
        LOG_ERROR("Failed to write PID file: " + str_pid_file);
        return false;
    }

    file << getpid();
    file.close();

    LOG_INFO("PID file written: " + str_pid_file + " (PID: " + std::to_string(getpid()) + ")");
    return true;
#endif
}

void CDaemon::RemovePidFile(const std::string& str_pid_file) {
#ifdef _WIN32
    // Windows: No PID files to remove - Services are managed by SCM
    (void)str_pid_file; // Suppress unused parameter warning
#else
    unlink(str_pid_file.c_str());
#endif
}

bool CDaemon::IsRunning(const std::string& str_pid_file) {
#ifdef _WIN32
    // Windows: No PID files - check service status via SCM instead
    (void)str_pid_file; // Suppress unused parameter warning
    // Note: For Windows Service status, query SCM using service APIs
    return false;
#else
    std::ifstream file(str_pid_file);
    if (!file.is_open()) {
        return false;
    }

    int n_pid;
    file >> n_pid;
    file.close();

    // Check if process exists
    if (kill(n_pid, 0) == 0) {
        return true;
    }

    // PID file exists but process doesn't, remove stale file
    RemovePidFile(str_pid_file);
    return false;
#endif
}

void CDaemon::SetupSignalHandlers() {
#ifndef _WIN32
    // POSIX: Setup signal handlers for SIGTERM and SIGINT
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);  // Ignore broken pipe
#else
    // Windows: Console control handler is set up in win_service.cpp
    // via SetupConsoleCtrlHandler() which is called from main()
    // This function is called from RunApplicationMain() after mode detection,
    // so the console control handler is already registered by that point.
    // Service mode doesn't need console handler - uses ServiceCtrlHandler instead.
    // Nothing to do here for Windows.
#endif
}
