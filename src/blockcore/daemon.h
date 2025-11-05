// ============= daemon.h =============
#ifndef DAEMON_H
#define DAEMON_H

#include <string>

#ifndef _WIN32
    #include <csignal>
#endif

class CDaemon {
public:
    // Daemonize the process
    static bool Daemonize(const std::string& str_pid_file = "/tmp/bweave.pid");

    // Write PID to file
    static bool WritePidFile(const std::string& str_pid_file);

    // Remove PID file
    static void RemovePidFile(const std::string& str_pid_file);

    // Check if daemon is already running
    static bool IsRunning(const std::string& str_pid_file);

    // Setup signal handlers for graceful shutdown
    // POSIX: Sets up SIGTERM/SIGINT handlers
    // Windows: Sets up console control handler (in console mode only)
    static void SetupSignalHandlers();
};

// Global flag for shutdown signal
// Set by signal handler (POSIX), console control handler (Windows console),
// or service control handler (Windows service)
#ifndef _WIN32
    extern volatile sig_atomic_t g_f_shutdown_requested;
#else
    extern volatile int g_f_shutdown_requested;
#endif

#endif
