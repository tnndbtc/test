// ============= daemon.cpp =============
#include "daemon.h"
#include "logger/logger.h"
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>

// Global shutdown flag
volatile sig_atomic_t g_f_shutdown_requested = 0;

// Signal handler
static void SignalHandler(int n_signal) {
    if (n_signal == SIGTERM || n_signal == SIGINT) {
        g_f_shutdown_requested = 1;
    }
}

bool CDaemon::Daemonize(const std::string& str_pid_file) {
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
}

bool CDaemon::WritePidFile(const std::string& str_pid_file) {
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
}

void CDaemon::RemovePidFile(const std::string& str_pid_file) {
    unlink(str_pid_file.c_str());
}

bool CDaemon::IsRunning(const std::string& str_pid_file) {
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
}

void CDaemon::SetupSignalHandlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);  // Ignore broken pipe
}
