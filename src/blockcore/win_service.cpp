// ============= win_service.cpp =============
#ifdef _WIN32

#include "win_service.h"
#include "daemon.h"
#include "logger/logger.h"
#include <iostream>
#include <windows.h>
#include <tchar.h>

// Global service status handle (set by RegisterServiceCtrlHandlerEx)
SERVICE_STATUS_HANDLE g_service_status_handle = NULL;

// Global service status structure (zero-initialized)
SERVICE_STATUS g_service_status = {};

// Static checkpoint counter for pending states
static DWORD s_dw_checkpoint = 1;

// Forward declaration of external shutdown flag
extern volatile int g_f_shutdown_requested;

/**
 * @brief Service main entry point called by SCM
 */
VOID WINAPI ServiceMain(DWORD dw_argc, LPTSTR* lpsz_argv) {
    // Register service control handler
    g_service_status_handle = RegisterServiceCtrlHandlerEx(
        TEXT(SERVICE_NAME),
        ServiceCtrlHandler,
        NULL);

    if (g_service_status_handle == NULL) {
        // Failed to register handler - log and return
        LOG_ERROR("RegisterServiceCtrlHandlerEx failed");
        return;
    }

    // Initialize service status structure
    ZeroMemory(&g_service_status, sizeof(g_service_status));
    g_service_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_service_status.dwServiceSpecificExitCode = 0;

    // Report initial status: starting
    SetServiceStatusWrapper(SERVICE_START_PENDING, NO_ERROR, 3000);

    // Log service startup
    LOG_INFO("Windows service starting...");

    // Perform service initialization
    // Convert arguments for RunApplication
    char** argv = new char*[dw_argc];
    for (DWORD i = 0; i < dw_argc; i++) {
        // Convert TCHAR to char (simplified - assumes ASCII)
        size_t len = _tcslen(lpsz_argv[i]);
        argv[i] = new char[len + 1];
        for (size_t j = 0; j <= len; j++) {
            argv[i][j] = (char)lpsz_argv[i][j];
        }
    }

    // Report status: running
    SetServiceStatusWrapper(SERVICE_RUNNING, NO_ERROR, 0);
    LOG_INFO("Windows service started successfully");

    // Run the main application (this blocks until shutdown)
    int exit_code = RunApplication(dw_argc, argv);

    // Cleanup arguments
    for (DWORD i = 0; i < dw_argc; i++) {
        delete[] argv[i];
    }
    delete[] argv;

    // Report final status: stopped
    LOG_INFO("Windows service stopping...");
    SetServiceStatusWrapper(SERVICE_STOPPED, exit_code, 0);
}

/**
 * @brief Service control handler for SCM events
 */
DWORD WINAPI ServiceCtrlHandler(DWORD dw_control, DWORD dw_event_type,
                                LPVOID lp_event_data, LPVOID lp_context) {
    (void)dw_event_type;  // Unused
    (void)lp_event_data;  // Unused
    (void)lp_context;     // Unused

    switch (dw_control) {
        case SERVICE_CONTROL_STOP:
            // Service stop requested - initiate graceful shutdown
            LOG_INFO("Service STOP request received");
            SetServiceStatusWrapper(SERVICE_STOP_PENDING, NO_ERROR, 5000);

            // Set shutdown flag (same flag used by POSIX signals)
            g_f_shutdown_requested = 1;

            return NO_ERROR;

        case SERVICE_CONTROL_SHUTDOWN:
            // System shutdown - fast shutdown
            LOG_INFO("Service SHUTDOWN request received (system shutting down)");
            SetServiceStatusWrapper(SERVICE_STOP_PENDING, NO_ERROR, 1000);

            // Set shutdown flag
            g_f_shutdown_requested = 1;

            return NO_ERROR;

        case SERVICE_CONTROL_INTERROGATE:
            // Status query - report current status
            SetServiceStatusWrapper(g_service_status.dwCurrentState, NO_ERROR, 0);
            return NO_ERROR;

        case SERVICE_CONTROL_PAUSE:
        case SERVICE_CONTROL_CONTINUE:
            // Pause/Continue not supported
            return ERROR_CALL_NOT_IMPLEMENTED;

        default:
            // Unknown control code
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

/**
 * @brief Console control handler for Ctrl+C, Ctrl+Break, Close
 */
BOOL WINAPI ConsoleCtrlHandler(DWORD dw_ctrl_type) {
    switch (dw_ctrl_type) {
        case CTRL_C_EVENT:
            std::cout << "\n[Console] Ctrl+C received, initiating graceful shutdown...\n";
            LOG_INFO("Ctrl+C received, initiating graceful shutdown");
            g_f_shutdown_requested = 1;
            return TRUE;

        case CTRL_BREAK_EVENT:
            std::cout << "\n[Console] Ctrl+Break received, initiating graceful shutdown...\n";
            LOG_INFO("Ctrl+Break received, initiating graceful shutdown");
            g_f_shutdown_requested = 1;
            return TRUE;

        case CTRL_CLOSE_EVENT:
            std::cout << "\n[Console] Console closing, initiating graceful shutdown...\n";
            LOG_INFO("Console close event received, initiating graceful shutdown");
            g_f_shutdown_requested = 1;
            // Give time for graceful shutdown
            Sleep(5000);
            return TRUE;

        case CTRL_LOGOFF_EVENT:
            // User logging off - only stop if interactive service
            LOG_INFO("User logoff event received");
            return FALSE;

        case CTRL_SHUTDOWN_EVENT:
            std::cout << "\n[Console] System shutdown, initiating fast shutdown...\n";
            LOG_INFO("System shutdown event received");
            g_f_shutdown_requested = 1;
            Sleep(1000);
            return TRUE;

        default:
            return FALSE;
    }
}

/**
 * @brief Set service status and report to SCM
 */
void SetServiceStatusWrapper(DWORD dw_current_state, DWORD dw_win32_exit_code, DWORD dw_wait_hint) {
    // Update current state
    g_service_status.dwCurrentState = dw_current_state;
    g_service_status.dwWin32ExitCode = dw_win32_exit_code;
    g_service_status.dwWaitHint = dw_wait_hint;

    // Set controls accepted based on state
    if (dw_current_state == SERVICE_START_PENDING) {
        g_service_status.dwControlsAccepted = 0;
    } else {
        g_service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }

    // Update checkpoint for pending states
    if ((dw_current_state == SERVICE_RUNNING) || (dw_current_state == SERVICE_STOPPED)) {
        g_service_status.dwCheckPoint = 0;
    } else {
        g_service_status.dwCheckPoint = s_dw_checkpoint++;
    }

    // Report status to SCM
    SetServiceStatus(g_service_status_handle, &g_service_status);
}

/**
 * @brief Install bweave as Windows service
 */
bool InstallService(const std::string& str_executable_path) {
    SC_HANDLE h_sc_manager = NULL;
    SC_HANDLE h_service = NULL;
    bool f_success = false;

    std::cout << "[Service] Installing Windows service...\n";
    std::cout << "[Service] Executable: " << str_executable_path << "\n";

    // Open Service Control Manager
    h_sc_manager = OpenSCManager(
        NULL,                    // Local computer
        NULL,                    // ServicesActive database
        SC_MANAGER_CREATE_SERVICE);  // Rights needed

    if (h_sc_manager == NULL) {
        std::cerr << "[Service] Failed to open Service Control Manager\n";
        std::cerr << "[Service] Error code: " << GetLastError() << "\n";
        std::cerr << "[Service] Hint: Run as Administrator\n";
        return false;
    }

    // Build service command line (include --service flag)
    std::string str_service_cmd = "\"" + str_executable_path + "\" --service";

    // Create service
    h_service = CreateService(
        h_sc_manager,                           // SCManager database
        TEXT(SERVICE_NAME),                     // Service name
        TEXT(SERVICE_DISPLAY_NAME),             // Display name
        SERVICE_ALL_ACCESS,                     // Desired access
        SERVICE_WIN32_OWN_PROCESS,              // Service type
        SERVICE_AUTO_START,                     // Start type (automatic)
        SERVICE_ERROR_NORMAL,                   // Error control
        TEXT(str_service_cmd.c_str()),          // Binary path
        NULL,                                   // No load ordering group
        NULL,                                   // No tag identifier
        NULL,                                   // No dependencies
        NULL,                                   // LocalSystem account
        NULL);                                  // No password

    if (h_service == NULL) {
        DWORD dw_error = GetLastError();
        if (dw_error == ERROR_SERVICE_EXISTS) {
            std::cerr << "[Service] Service already exists\n";
        } else {
            std::cerr << "[Service] Failed to create service\n";
            std::cerr << "[Service] Error code: " << dw_error << "\n";
        }
        CloseServiceHandle(h_sc_manager);
        return false;
    }

    // Set service description
    SERVICE_DESCRIPTION sd;
    sd.lpDescription = const_cast<LPTSTR>(TEXT(BWEAVE_SERVICE_DESCRIPTION));
    ChangeServiceConfig2(h_service, SERVICE_CONFIG_DESCRIPTION, &sd);

    std::cout << "[Service] Service installed successfully\n";
    std::cout << "[Service] Service name: " << SERVICE_NAME << "\n";
    std::cout << "[Service] Display name: " << SERVICE_DISPLAY_NAME << "\n";
    std::cout << "[Service] Start service with: net start " << SERVICE_NAME << "\n";
    std::cout << "[Service] Or use Services console (services.msc)\n";

    f_success = true;

    // Cleanup
    CloseServiceHandle(h_service);
    CloseServiceHandle(h_sc_manager);

    return f_success;
}

/**
 * @brief Uninstall bweave Windows service
 */
bool UninstallService() {
    SC_HANDLE h_sc_manager = NULL;
    SC_HANDLE h_service = NULL;
    SERVICE_STATUS service_status;
    bool f_success = false;

    std::cout << "[Service] Uninstalling Windows service...\n";

    // Open Service Control Manager
    h_sc_manager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (h_sc_manager == NULL) {
        std::cerr << "[Service] Failed to open Service Control Manager\n";
        std::cerr << "[Service] Error code: " << GetLastError() << "\n";
        std::cerr << "[Service] Hint: Run as Administrator\n";
        return false;
    }

    // Open service
    h_service = OpenService(h_sc_manager, TEXT(SERVICE_NAME), SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    if (h_service == NULL) {
        DWORD dw_error = GetLastError();
        if (dw_error == ERROR_SERVICE_DOES_NOT_EXIST) {
            std::cout << "[Service] Service is not installed\n";
        } else {
            std::cerr << "[Service] Failed to open service\n";
            std::cerr << "[Service] Error code: " << dw_error << "\n";
        }
        CloseServiceHandle(h_sc_manager);
        return false;
    }

    // Stop service if running
    if (QueryServiceStatus(h_service, &service_status)) {
        if (service_status.dwCurrentState != SERVICE_STOPPED) {
            std::cout << "[Service] Stopping service...\n";
            if (ControlService(h_service, SERVICE_CONTROL_STOP, &service_status)) {
                // Wait for service to stop (max 30 seconds)
                for (int i = 0; i < 30; i++) {
                    Sleep(1000);
                    if (!QueryServiceStatus(h_service, &service_status)) {
                        break;
                    }
                    if (service_status.dwCurrentState == SERVICE_STOPPED) {
                        std::cout << "[Service] Service stopped\n";
                        break;
                    }
                }
            }
        }
    }

    // Delete service
    if (DeleteService(h_service)) {
        std::cout << "[Service] Service uninstalled successfully\n";
        f_success = true;
    } else {
        DWORD dw_error = GetLastError();
        std::cerr << "[Service] Failed to delete service\n";
        std::cerr << "[Service] Error code: " << dw_error << "\n";
        if (dw_error == ERROR_SERVICE_MARKED_FOR_DELETE) {
            std::cout << "[Service] Service is already marked for deletion\n";
            std::cout << "[Service] Restart system to complete removal\n";
        }
    }

    // Cleanup
    CloseServiceHandle(h_service);
    CloseServiceHandle(h_sc_manager);

    return f_success;
}

/**
 * @brief Check if running as Windows service
 */
bool IsRunningAsService() {
    // Try to get stdout handle
    HANDLE h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);

    // If stdout is NULL or invalid, we're running as a service
    // Services don't have console handles
    if (h_stdout == NULL || h_stdout == INVALID_HANDLE_VALUE) {
        return true;
    }

    // Additional check: try to get console window
    HWND h_console = GetConsoleWindow();
    if (h_console == NULL) {
        return true;
    }

    return false;
}

/**
 * @brief Start the Windows service dispatcher
 */
bool StartServiceDispatcher(int argc, char* argv[]) {
    (void)argc;  // Unused - service gets args from SCM
    (void)argv;  // Unused

    // Service table entry
    SERVICE_TABLE_ENTRY service_table[] = {
        { const_cast<LPTSTR>(TEXT(SERVICE_NAME)), ServiceMain },
        { NULL, NULL }
    };

    // Connect to Service Control Manager
    // This call blocks until service stops
    if (!StartServiceCtrlDispatcher(service_table)) {
        DWORD dw_error = GetLastError();
        if (dw_error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            // Not running as service - this is expected in console mode
            return false;
        }

        std::cerr << "[Service] StartServiceCtrlDispatcher failed\n";
        std::cerr << "[Service] Error code: " << dw_error << "\n";
        return false;
    }

    return true;
}

/**
 * @brief Setup console control handler for console mode
 */
bool SetupConsoleCtrlHandler() {
    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
        std::cerr << "[Console] Failed to register console control handler\n";
        std::cerr << "[Console] Error code: " << GetLastError() << "\n";
        return false;
    }

    LOG_INFO("Console control handler registered");
    return true;
}

// Forward declaration of main application function from main.cpp
extern int RunApplicationMain(const std::string& str_config_file, bool f_daemon_mode);

/**
 * @brief Run the main application logic
 *
 * Called from ServiceMain in service mode.
 * Calls into the main application function defined in main.cpp.
 */
int RunApplication(int argc, char* argv[]) {
    // Parse arguments for config file (if provided by SCM)
    std::string str_config_file = "bweave.conf";
    for (int n_i = 1; n_i < argc; n_i++) {
        std::string str_arg = argv[n_i];
        if ((str_arg == "-c" || str_arg == "--config") && n_i + 1 < argc) {
            str_config_file = argv[++n_i];
        }
    }

    // Service mode never uses daemon mode (that's POSIX only)
    bool f_daemon_mode = false;

    // Call main application logic
    return RunApplicationMain(str_config_file, f_daemon_mode);
}

#endif // _WIN32
