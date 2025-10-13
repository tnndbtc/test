// ============= daemon.h =============
#ifndef DAEMON_H
#define DAEMON_H

#include <string>

class CDaemon {
public:
    // Daemonize the process
    static bool Daemonize(const std::string& str_pid_file = "/tmp/rest_daemon.pid");

    // Write PID to file
    static bool WritePidFile(const std::string& str_pid_file);

    // Remove PID file
    static void RemovePidFile(const std::string& str_pid_file);

    // Check if daemon is already running
    static bool IsRunning(const std::string& str_pid_file);

    // Setup signal handlers for graceful shutdown
    static void SetupSignalHandlers();
};

// Global flag for shutdown signal
extern volatile sig_atomic_t g_f_shutdown_requested;

#endif
