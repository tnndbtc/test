// ============= win_service.h =============
#ifndef WIN_SERVICE_H
#define WIN_SERVICE_H

#ifdef _WIN32

#include <windows.h>
#include <string>

/**
 * @file win_service.h
 * @brief Windows Service wrapper for bweave daemon
 *
 * Provides Windows Service Control Manager (SCM) integration for running
 * bweave as a Windows service. Also includes console control handler for
 * running in console mode with Ctrl+C support.
 *
 * Service Modes:
 * 1. Service Mode: Runs under SCM control, managed via services.msc
 * 2. Console Mode: Runs as foreground process with Ctrl+C handling
 * 3. Console Daemon Mode: Runs detached from console with PID file
 */

// Global service status handle
extern SERVICE_STATUS_HANDLE g_service_status_handle;

// Global service status structure
extern SERVICE_STATUS g_service_status;

// Service configuration
const char* const SERVICE_NAME = "bweave";
const char* const SERVICE_DISPLAY_NAME = "Blockweave Daemon";
const char* const BWEAVE_SERVICE_DESCRIPTION = "Blockweave blockchain daemon with REST API and P2P mining";

/**
 * @brief Service main entry point called by SCM
 * @param dw_argc Number of arguments
 * @param lpsz_argv Array of argument strings
 *
 * Called by Service Control Manager when service starts.
 * Registers control handler and runs the main application loop.
 */
VOID WINAPI ServiceMain(DWORD dw_argc, LPTSTR* lpsz_argv);

/**
 * @brief Service control handler for SCM events
 * @param dw_control Control code from SCM
 * @param dw_event_type Event type (unused)
 * @param lp_event_data Event data (unused)
 * @param lp_context Context pointer (unused)
 * @return ERROR_SUCCESS or error code
 *
 * Handles control events from Service Control Manager:
 * - SERVICE_CONTROL_STOP: Initiate graceful shutdown
 * - SERVICE_CONTROL_SHUTDOWN: System shutdown (fast shutdown)
 * - SERVICE_CONTROL_INTERROGATE: Report current status
 */
DWORD WINAPI ServiceCtrlHandler(DWORD dw_control, DWORD dw_event_type,
                                LPVOID lp_event_data, LPVOID lp_context);

/**
 * @brief Console control handler for Ctrl+C, Ctrl+Break, Close
 * @param dw_ctrl_type Control event type
 * @return TRUE if handled, FALSE otherwise
 *
 * Handles console control events when running in console mode:
 * - CTRL_C_EVENT: Ctrl+C pressed
 * - CTRL_BREAK_EVENT: Ctrl+Break pressed
 * - CTRL_CLOSE_EVENT: Console window closed
 * - CTRL_LOGOFF_EVENT: User logging off
 * - CTRL_SHUTDOWN_EVENT: System shutting down
 */
BOOL WINAPI ConsoleCtrlHandler(DWORD dw_ctrl_type);

/**
 * @brief Set service status and report to SCM
 * @param dw_current_state Current service state (RUNNING, STOPPED, etc.)
 * @param dw_win32_exit_code Win32 exit code (0 for success)
 * @param dw_wait_hint Estimated time for pending operation (milliseconds)
 *
 * Updates service status structure and reports to Service Control Manager.
 * Automatically increments checkpoint for pending states.
 */
void SetServiceStatusWrapper(DWORD dw_current_state, DWORD dw_win32_exit_code, DWORD dw_wait_hint);

/**
 * @brief Install bweave as Windows service
 * @param str_executable_path Full path to bweave.exe
 * @return true if installation succeeded, false otherwise
 *
 * Creates service entry in Windows Service Control Manager.
 * Service is configured for automatic startup.
 * Requires administrator privileges.
 */
bool InstallService(const std::string& str_executable_path);

/**
 * @brief Uninstall bweave Windows service
 * @return true if uninstallation succeeded, false otherwise
 *
 * Removes service entry from Windows Service Control Manager.
 * Stops service if currently running.
 * Requires administrator privileges.
 */
bool UninstallService();

/**
 * @brief Check if running as Windows service
 * @return true if running as service, false if console mode
 *
 * Detects whether process is running under Service Control Manager
 * by checking if stdout handle is valid (console) or null (service).
 */
bool IsRunningAsService();

/**
 * @brief Start the Windows service dispatcher
 * @param argc Number of command-line arguments
 * @param argv Array of command-line argument strings
 * @return true if service ran successfully, false on error
 *
 * Connects process to Service Control Manager and blocks until
 * service stops. Only call this when running as service.
 */
bool StartServiceDispatcher(int argc, char* argv[]);

/**
 * @brief Setup console control handler for console mode
 * @return true if handler registered successfully, false otherwise
 *
 * Registers ConsoleCtrlHandler for handling Ctrl+C and other
 * console events in console mode (non-service mode).
 */
bool SetupConsoleCtrlHandler();

/**
 * @brief Run the main application logic
 * @param argc Number of command-line arguments
 * @param argv Array of command-line argument strings
 * @return Exit code (0 for success)
 *
 * Main application entry point called by either ServiceMain (service mode)
 * or main() (console mode). Contains the main daemon loop that runs
 * the REST API server, peer manager, and mining manager.
 */
int RunApplication(int argc, char* argv[]);

#endif // _WIN32

#endif // WIN_SERVICE_H
