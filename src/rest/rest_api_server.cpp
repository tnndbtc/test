// ============= rest_api_server.cpp =============
/**
 * @file rest_api_server.cpp
 * @brief Implementation of multi-threaded HTTP REST API server
 *
 * Provides HTTP REST API access to blockweave operations including:
 * - Blockchain state queries
 * - Transaction submission
 * - File uploads as transactions
 * - Mining control
 *
 * Uses listener/worker thread architecture with request queue for
 * concurrent request processing.
 */

#include "rest_api_server.h"
#include "peer/peer_manager.h"
#include "peer/peer_node.h"
#include "utils/config.h"
#include "utils/threadname.h"
#include "utils/httpcode.h"
#include "utils/time_util.h"
#include "logger/logger.h"
#include "blockcore/transaction.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <cctype>

// Boost.Beast and Asio headers
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <fstream>
#include <random>
#include <iomanip>
#include <sys/stat.h>

// Platform-specific includes for directory operations
#ifdef _WIN32
    #include <direct.h>  // For _mkdir
    #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif

// Namespace aliases for convenience
namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

// ============= Utility Functions =============

/**
 * @brief Extract value for a given key from simple JSON string
 * @param str_json JSON string to parse
 * @param str_key Key to search for
 * @return Extracted value as string, or empty string if not found
 *
 * Simple JSON parser that handles:
 * - String values (enclosed in quotes)
 * - Numeric/boolean values
 * - Trimmed whitespace
 *
 * Note: This is a lightweight parser for basic JSON. For complex JSON,
 * consider using a dedicated JSON library.
 */
static std::string ExtractJsonValue(const std::string& str_json, const std::string& str_key) {
    // Find the key
    std::string str_search = "\"" + str_key + "\"";
    size_t n_key_pos = str_json.find(str_search);
    if (n_key_pos == std::string::npos) {
        return "";
    }

    // Find the colon after the key
    size_t n_colon_pos = str_json.find(':', n_key_pos);
    if (n_colon_pos == std::string::npos) {
        return "";
    }

    // Skip whitespace after colon
    size_t n_value_start = n_colon_pos + 1;
    while (n_value_start < str_json.length() && std::isspace(str_json[n_value_start])) {
        n_value_start++;
    }

    // Check if value is a string (starts with ")
    if (str_json[n_value_start] == '"') {
        n_value_start++;
        size_t n_value_end = str_json.find('"', n_value_start);
        if (n_value_end != std::string::npos) {
            return str_json.substr(n_value_start, n_value_end - n_value_start);
        }
    } else {
        // Numeric or boolean value - read until comma, }, or newline
        size_t n_value_end = n_value_start;
        while (n_value_end < str_json.length() &&
               str_json[n_value_end] != ',' &&
               str_json[n_value_end] != '}' &&
               str_json[n_value_end] != '\n') {
            n_value_end++;
        }
        std::string str_value = str_json.substr(n_value_start, n_value_end - n_value_start);
        // Trim whitespace
        size_t n_end = str_value.find_last_not_of(" \t\r\n");
        return (n_end != std::string::npos) ? str_value.substr(0, n_end + 1) : str_value;
    }

    return "";
}

/**
 * @brief Base64 character set for encoding/decoding
 */
static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

/**
 * @brief Check if character is valid Base64
 * @param c Character to check
 * @return true if valid Base64 character
 */
static bool IsBase64(unsigned char c) {
    return (std::isalnum(c) || (c == '+') || (c == '/'));
}

/**
 * @brief Decode Base64 string to binary data
 * @param str_encoded Base64-encoded string
 * @return Decoded binary data as byte vector
 *
 * Implements standard Base64 decoding (RFC 4648).
 * Handles padding ('=') characters correctly.
 * Returns empty vector if decoding fails.
 */
static std::vector<uint8_t> DecodeBase64(const std::string& str_encoded) {
    std::vector<uint8_t> decoded;
    size_t n_in_len = str_encoded.size();
    int n_i = 0;
    int n_j = 0;
    size_t n_in = 0;
    unsigned char char_array_4[4], char_array_3[3];

    while (n_in_len-- && (str_encoded[n_in] != '=') && IsBase64(str_encoded[n_in])) {
        char_array_4[n_i++] = str_encoded[n_in];
        n_in++;
        if (n_i == 4) {
            for (n_i = 0; n_i < 4; n_i++) {
                char_array_4[n_i] = static_cast<unsigned char>(base64_chars.find(char_array_4[n_i]));
            }

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (n_i = 0; n_i < 3; n_i++) {
                decoded.push_back(char_array_3[n_i]);
            }
            n_i = 0;
        }
    }

    if (n_i) {
        for (n_j = n_i; n_j < 4; n_j++) {
            char_array_4[n_j] = 0;
        }

        for (n_j = 0; n_j < 4; n_j++) {
            char_array_4[n_j] = static_cast<unsigned char>(base64_chars.find(char_array_4[n_j]));
        }

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (n_j = 0; (n_j < n_i - 1); n_j++) {
            decoded.push_back(char_array_3[n_j]);
        }
    }

    return decoded;
}

/**
 * @brief Extract filename and file data from multipart/form-data
 * @param str_body HTTP request body containing multipart data
 * @param str_boundary Boundary string from Content-Type header
 * @param str_filename Output parameter for extracted filename
 * @param file_data Output parameter for extracted file binary data
 * @return true if parsing succeeded, false otherwise
 *
 * Parses multipart/form-data format (RFC 2388):
 * 1. Finds boundary markers
 * 2. Extracts filename from Content-Disposition header
 * 3. Locates file data between headers and next boundary
 * 4. Returns binary file data and filename
 */
/*
static bool ParseMultipartFile(const std::string& str_body, const std::string& str_boundary,
                               std::string& str_filename, std::vector<uint8_t>& file_data) {
    // Find boundary markers
    std::string str_start_boundary = "--" + str_boundary;
    std::string str_end_boundary = "--" + str_boundary + "--";

    size_t n_start = str_body.find(str_start_boundary);
    if (n_start == std::string::npos) {
        return false;
    }

    // Skip past first boundary
    n_start += str_start_boundary.length();

    // Find Content-Disposition header
    size_t n_disposition = str_body.find("Content-Disposition:", n_start);
    if (n_disposition == std::string::npos) {
        return false;
    }

    // Extract filename from Content-Disposition
    size_t n_filename_start = str_body.find("filename=\"", n_disposition);
    if (n_filename_start != std::string::npos) {
        n_filename_start += 10; // Length of "filename=\""
        size_t n_filename_end = str_body.find("\"", n_filename_start);
        if (n_filename_end != std::string::npos) {
            str_filename = str_body.substr(n_filename_start, n_filename_end - n_filename_start);
        }
    }

    // Find blank line marking start of file data
    size_t n_data_start = str_body.find("\r\n\r\n", n_disposition);
    if (n_data_start == std::string::npos) {
        n_data_start = str_body.find("\n\n", n_disposition);
        if (n_data_start == std::string::npos) {
            return false;
        }
        n_data_start += 2;
    } else {
        n_data_start += 4;
    }

    // Find end boundary
    size_t n_data_end = str_body.find(str_start_boundary, n_data_start);
    if (n_data_end == std::string::npos) {
        return false;
    }

    // Remove trailing CRLF before boundary
    while (n_data_end > n_data_start &&
           (str_body[n_data_end - 1] == '\n' || str_body[n_data_end - 1] == '\r')) {
        n_data_end--;
    }

    // Extract file data
    for (size_t i = n_data_start; i < n_data_end; i++) {
        file_data.push_back(static_cast<uint8_t>(str_body[i]));
    }

    return !file_data.empty();
}
*/
/**
 * @brief Generate UUID v4 string
 * @return UUID v4 string in format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
 *
 * Generates random UUID version 4 (RFC 4122):
 * - Uses random device for entropy
 * - Sets version bits to 0100 (version 4)
 * - Sets variant bits to 10xx (RFC 4122)
 * - Returns lowercase hexadecimal UUID string
 */
/*
static std::string GenerateUUID() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    ss << std::hex;
    for (int n_i = 0; n_i < 8; n_i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (int n_i = 0; n_i < 4; n_i++) {
        ss << dis(gen);
    }
    ss << "-4";  // UUID version 4
    for (int n_i = 0; n_i < 3; n_i++) {
        ss << dis(gen);
    }
    ss << "-";
    ss << dis2(gen);  // UUID variant (8, 9, A, or B)
    for (int n_i = 0; n_i < 3; n_i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (int n_i = 0; n_i < 12; n_i++) {
        ss << dis(gen);
    }
    return ss.str();
}
*/
/**
 * @brief Create directory recursively (like mkdir -p)
 * @param str_path Directory path to create
 * @return true if directory exists or was created successfully
 *
 * Creates directory and all parent directories as needed.
 * Uses Unix permissions 0755 (rwxr-xr-x).
 * Returns true if directory already exists.
 */
/*
static bool CreateDirectoryRecursive(const std::string& str_path) {
    if (str_path.empty()) return false;

    // Check if directory already exists
    struct stat st;
    if (stat(str_path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    // Find parent directory
    size_t n_pos = str_path.find_last_of('/');
    if (n_pos != std::string::npos && n_pos > 0) {
        std::string str_parent = str_path.substr(0, n_pos);
        if (!CreateDirectoryRecursive(str_parent)) {
            return false;
        }
    }

    // Create this directory
#ifdef _WIN32
    return _mkdir(str_path.c_str()) == 0;
#else
    return mkdir(str_path.c_str(), 0755) == 0;
#endif
}
*/
// ============= CRestApiServer Implementation =============

/**
 * @brief Construct REST API server
 * @param p_weave Pointer to blockweave instance
 * @param p_peer_mgr Pointer to peer manager instance
 * @param p_cfg Pointer to configuration object
 * @param str_miner_addr Mining reward address
 * @param n_port_num HTTP server port
 *
 * Initializes server in stopped state. Reserves space for worker
 * threads (REST_WORKER_THREADS from settings.h, default 5).
 * Creates shared request queue for listener/worker coordination.
 */
CRestApiServer::CRestApiServer(CBlockweave* p_weave, CPeerManager* p_peer_mgr,
                               const CConfig* p_cfg, const std::string& str_miner_addr,
                               int n_port_num)
    : p_blockweave(p_weave), p_peer_manager(p_peer_mgr), p_config(p_cfg),
      str_miner_address(str_miner_addr), n_port(n_port_num),
      f_running(false), f_stop_requested(false),
      p_request_queue(std::make_shared<CRequestQueue>()),
      m_str_cookie_username("__cookie__"),
      m_str_cookie_password(""),
      m_f_auth_enabled(false) {

    m_worker_threads.reserve(REST_WORKER_THREADS);
}

/**
 * @brief Destructor - stops server and cleans up resources
 *
 * Calls Stop() to ensure all threads are joined and sockets closed.
 * Safe to call even if server was never started.
 */
CRestApiServer::~CRestApiServer() {
    Stop();
}

/**
 * @brief Start REST API server
 * @return true if started successfully, false on error
 *
 * Startup sequence:
 * 1. Create IPv4 TCP socket
 * 2. Set SO_REUSEADDR option for rapid restart
 * 3. Bind to INADDR_ANY (all interfaces) on configured port
 * 4. Start listening with backlog of 10
 * 5. Set running flags
 * 6. Start listener thread
 * 7. Start REST_WORKER_THREADS worker threads (default: 5)
 *
 * Returns false if socket creation, bind, or listen fails.
 */
bool CRestApiServer::Start() {
    if (f_running) {
        LOG_WARN("REST API server already running");
        return false;
    }

    if (!p_config) {
        LOG_ERROR("Cannot start REST API server: Config is null");
        return false;
    }

    try {
        LOG_TRACE("Creating REST API server acceptor on port " + std::to_string(n_port));

        // Create acceptor
        m_acceptor = std::make_unique<tcp::acceptor>(
            m_io_context,
            tcp::endpoint(tcp::v4(), static_cast<uint16_t>(n_port))
        );

        // Enable SO_REUSEADDR
        m_acceptor->set_option(asio::socket_base::reuse_address(true));

        LOG_INFO("REST API server listening on port " + std::to_string(n_port));

        // Load RPC authentication credentials (REQUIRED)
        try {
            std::string str_network = p_config->GetNetwork();
            std::string str_data_dir = p_config->GetNetworkDataDir(str_network);
            std::string str_cookie_path = str_data_dir + "/.cookie";

            if (LoadCookieFile(str_cookie_path)) {
                m_f_auth_enabled = true;
                LOG_INFO("RPC authentication enabled using: " + str_cookie_path);
            } else {
                // .cookie file missing or invalid - FAIL TO START
                m_f_auth_enabled = false;
                LOG_ERROR("REST API server requires .cookie file: " + str_cookie_path);
                LOG_ERROR("Generate with: ./wallet or manually create");
                m_acceptor.reset();
                return false;  // Fail to start
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Error loading RPC authentication: " + std::string(e.what()));
            m_acceptor.reset();
            return false;  // Fail to start
        }

        // Reset flags
        f_stop_requested = false;
        f_running = true;

        // Reset request queue for restart (clears shutdown flag from previous Stop())
        p_request_queue->Reset();

        // Start listener thread
        m_listener_thread = std::thread(&CRestApiServer::ListenerThread, this);

        // Start worker threads
        for (size_t n_i = 0; n_i < REST_WORKER_THREADS; n_i++) {
            m_worker_threads.emplace_back(&CRestApiServer::WorkerThread, this, static_cast<int>(n_i));
        }

        LOG_TRACE("REST API worker threads: " + std::to_string(REST_WORKER_THREADS));

        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Failed to start REST API server: " + std::string(e.what()));
        f_running = false;
        return false;
    } catch (...) {
        LOG_ERROR("Failed to start REST API server: Unknown exception");
        f_running = false;
        return false;
    }
}

/**
 * @brief Stop REST API server
 *
 * Shutdown sequence (thread-safe and idempotent):
 * 1. Check if running (safe to call when stopped)
 * 2. Set stop flags
 * 3. Shutdown request queue (unblocks waiting workers)
 * 4. Close listening socket (unblocks listener)
 * 5. Join listener thread
 * 6. Join all worker threads
 *
 * Blocks until all threads have terminated.
 */
void CRestApiServer::Stop() {
    if (!f_running) {
        return;
    }

    LOG_INFO("Stopping REST API server...");
    f_stop_requested = true;

    // Make a dummy connection to unblock accept() before closing acceptor
    // The REST API server uses synchronous (blocking) I/O, not asynchronous I/O.
    // The listener thread blocks waiting for connections with accept(), and in
    // some implementations, closing the acceptor doesn't properly wake up the blocked thread.
    // thus, caused hanging on m_listener_thread.join();
    try {
        tcp::socket dummy_socket(m_io_context);
        tcp::endpoint endpoint(asio::ip::address_v4::loopback(), static_cast<uint16_t>(n_port));
        boost::system::error_code ec;
        dummy_socket.connect(endpoint, ec);
        // Ignore errors - acceptor might already be closed
        if (!ec) {
            dummy_socket.close();
        }
    } catch (...) {
        // Ignore any exceptions - just a best-effort to unblock
    }

    // Close acceptor to unblock listener
    if (m_acceptor && m_acceptor->is_open()) {
        boost::system::error_code ec;
        m_acceptor->close(ec);
        if (ec) {
            LOG_ERROR("Error closing acceptor: " + ec.message());
        }
    }

    // Shutdown request queue to unblock workers
    p_request_queue->Shutdown();

    // Join listener thread
    if (m_listener_thread.joinable()) {
        m_listener_thread.join();
    }

    // Join all worker threads
    for (auto& thread : m_worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    // Clear worker threads vector for clean restart
    m_worker_threads.clear();

    f_running = false;

    LOG_INFO("REST API server stopped");
}

/**
 * @brief Check if server is running
 * @return true if running, false otherwise
 *
 * Thread-safe read of atomic flag.
 */
bool CRestApiServer::IsRunning() const {
    return f_running;
}

/**
 * @brief Load RPC authentication credentials from .cookie file
 */
bool CRestApiServer::LoadCookieFile(const std::string& str_cookie_path) {
    std::ifstream cookie_file(str_cookie_path);
    if (!cookie_file.is_open()) {
        LOG_ERROR("Failed to open cookie file: " + str_cookie_path);
        return false;
    }

    std::string line;
    if (!std::getline(cookie_file, line)) {
        LOG_ERROR("Failed to read cookie file");
        return false;
    }

    cookie_file.close();

    // Parse format: __cookie__:password
    size_t colon_pos = line.find(':');
    if (colon_pos == std::string::npos) {
        LOG_ERROR("Invalid cookie file format (missing colon)");
        return false;
    }

    std::string username = line.substr(0, colon_pos);
    std::string password = line.substr(colon_pos + 1);

    // Validate format
    if (username != "__cookie__") {
        LOG_ERROR("Invalid cookie file username (expected '__cookie__')");
        return false;
    }

    if (password.length() != 64) {
        LOG_ERROR("Invalid cookie file password length (expected 64 hex chars)");
        return false;
    }

    // Validate hex characters
    for (char c : password) {
        if (!std::isxdigit(c)) {
            LOG_ERROR("Invalid cookie file password (non-hex character)");
            return false;
        }
    }

    m_str_cookie_password = password;
    LOG_TRACE("Loaded RPC authentication credentials");
    return true;
}

/**
 * @brief Check if endpoint requires authentication
 */
bool CRestApiServer::RequiresAuth(const std::string& str_path) const {
    // Check if path starts with /rpc/
    return str_path.length() >= 5 && str_path.substr(0, 5) == "/rpc/";
}

/**
 * @brief Validate HTTP Basic Auth credentials
 */
bool CRestApiServer::ValidateBasicAuth(const std::string& str_auth_header) const {
    if (!m_f_auth_enabled) {
        return true; // Auth disabled, allow all requests
    }

    // Check for "Basic " prefix (case-insensitive)
    if (str_auth_header.length() < 6) {
        return false;
    }

    std::string prefix = str_auth_header.substr(0, 6);
    std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
    if (prefix != "basic ") {
        return false;
    }

    // Extract Base64-encoded credentials
    std::string str_encoded = str_auth_header.substr(6);

    // Decode Base64
    std::vector<uint8_t> decoded = DecodeBase64(str_encoded);
    if (decoded.empty()) {
        return false;
    }

    // Convert to string
    std::string str_credentials(decoded.begin(), decoded.end());

    // Parse username:password
    size_t colon_pos = str_credentials.find(':');
    if (colon_pos == std::string::npos) {
        return false;
    }

    std::string username = str_credentials.substr(0, colon_pos);
    std::string password = str_credentials.substr(colon_pos + 1);

    // Validate username
    if (username != m_str_cookie_username) {
        return false;
    }

    // Constant-time password comparison (prevent timing attacks)
    if (password.length() != m_str_cookie_password.length()) {
        return false;
    }

    int result = 0;
    for (size_t i = 0; i < password.length(); ++i) {
        result |= password[i] ^ m_str_cookie_password[i];
    }

    return result == 0;
}

/**
 * @brief Listener thread function - accepts connections and enqueues requests
 *
 * Main accept loop:
 * 1. Accept incoming TCP connection
 * 2. Read HTTP request from socket (up to 4KB)
 * 3. Parse request into CHttpRequest structure
 * 4. Enqueue request for worker processing
 *
 * Worker thread will handle response and close socket.
 * Exits when f_stop_requested is set and socket is closed.
 */
void CRestApiServer::ListenerThread() {
    // Set thread name for easier debugging and logging
    SetThreadName("rest_listener");

    LOG_INFO("REST API listener thread started");

    while (!f_stop_requested) {
        try {
            // Accept connection (blocking)
            tcp::socket socket(m_io_context);
            m_acceptor->accept(socket);

            // Check for shutdown immediately after accept
            if (f_stop_requested) {
                socket.close();
                break;
            }

            // Extract socket FD for queue
            int n_client_fd = static_cast<int>(socket.native_handle());

            // Release socket ownership (worker will wrap it)
            // Suppress C4996 warning for socket.release() on Windows < 8.1
            // This is a known Boost.Asio limitation, but we handle it properly
#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable: 4996)
#endif
            socket.release();
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

            // Create request object with socket FD
            CHttpRequest request;
            request.n_client_socket = n_client_fd;

            // Enqueue for worker processing
            p_request_queue->Enqueue(request);

        } catch (const boost::system::system_error& e) {
            if (f_stop_requested) {
                break;  // Acceptor closed during shutdown
            }
            LOG_ERROR("Accept error: " + std::string(e.what()));
        }
    }

    LOG_INFO("REST API listener thread stopped");
}

/**
 * @brief Worker thread function - processes requests from queue
 * @param n_worker_id Worker thread identifier (0-based)
 *
 * Processing loop:
 * 1. Dequeue request from queue (100ms timeout)
 * 2. Process request and send response
 * 3. Close client socket
 * 4. Repeat until shutdown
 *
 * Uses short timeout to check f_stop_requested frequently.
 */
void CRestApiServer::WorkerThread(int n_worker_id) {
    // Set thread name for easier debugging and logging
    SetThreadName("rest_worker" + std::to_string(n_worker_id));

    LOG_INFO("REST API worker " + std::to_string(n_worker_id) + " started");

    while (!f_stop_requested) {
        CHttpRequest request;

        // Wait for request with timeout
        if (!p_request_queue->Dequeue(request, 100)) {
            continue;  // Timeout or shutdown
        }

        try {
            // Wrap socket FD with Beast socket
            tcp::socket socket(m_io_context);
            socket.assign(tcp::v4(), request.n_client_socket);

            // Read HTTP request
            beast::flat_buffer buffer;
            http::request<http::string_body> req;
            http::read(socket, buffer, req);

            // Convert Beast request to CHttpRequest for handlers
            CHttpRequest parsed_req;
            parsed_req.str_method = std::string(req.method_string());
            parsed_req.str_path = std::string(req.target());
            parsed_req.str_body = req.body();
            parsed_req.str_content_type = std::string(req[http::field::content_type]);
            parsed_req.n_client_socket = request.n_client_socket;

            // Check if endpoint requires authentication
            if (RequiresAuth(parsed_req.str_path)) {
                std::string str_auth_header = std::string(req[http::field::authorization]);

                if (!ValidateBasicAuth(str_auth_header)) {
                    // Authentication failed - return 401 Unauthorized
                    http::response<http::string_body> res;
                    res.version(11);
                    res.result(http::status::unauthorized);
                    res.set(http::field::server, "blockweave/1.0");
                    res.set(http::field::www_authenticate, "Basic realm=\"Blockweave RPC\"");
                    res.set(http::field::content_type, "application/json");
                    res.set(http::field::connection, "close");
                    res.body() = R"({"error": "Unauthorized", "message": "Valid credentials required for RPC endpoints"})";
                    res.prepare_payload();

                    http::write(socket, res);

                    LOG_WARN("Unauthorized RPC request to " + parsed_req.str_path);

                    boost::system::error_code ec;
                    socket.shutdown(tcp::socket::shutdown_both, ec);
                    continue;
                }
            }

            // Route to handler (existing code)
            int n_status_code;
            std::string str_response_body;

            if (parsed_req.str_method == "GET") {
                std::tie(n_status_code, str_response_body) = HandleGET(parsed_req.str_path);
            } else if (parsed_req.str_method == "POST") {
                std::tie(n_status_code, str_response_body) = HandlePOST(parsed_req.str_path, parsed_req);
            } else {
                n_status_code = 405;  // Method Not Allowed
                str_response_body = R"({"error": "Method Not Allowed"})";
            }

            // Send response
            SendHttpResponse(socket, n_status_code, "application/json", str_response_body);

            // Graceful shutdown
            boost::system::error_code ec;
            socket.shutdown(tcp::socket::shutdown_both, ec);

        } catch (const boost::system::system_error& e) {
            LOG_ERROR("Worker error: " + std::string(e.what()));
            // Continue processing other requests
        } catch (const std::exception& e) {
            LOG_ERROR("Worker exception: " + std::string(e.what()));
        }
    }

    LOG_INFO("REST API worker " + std::to_string(n_worker_id) + " stopped");
}

void CRestApiServer::SendHttpResponse(tcp::socket& socket,
                                      int n_status_code,
                                      const std::string& str_content_type,
                                      const std::string& str_body) {
    try {
        // Build HTTP response
        http::response<http::string_body> res;
        res.version(11);  // HTTP/1.1
        res.result(static_cast<http::status>(n_status_code));
        res.set(http::field::server, "blockweave/1.0");
        res.set(http::field::content_type, str_content_type);
        res.set(http::field::connection, "close");
        res.body() = str_body;
        res.prepare_payload();  // Auto-sets Content-Length

        // Write response
        http::write(socket, res);

    } catch (const boost::system::system_error& e) {
        LOG_ERROR("Failed to send response: " + std::string(e.what()));
    }
}

std::tuple<int, std::string> CRestApiServer::HandleGetChain() {
    size_t n_mempool_size = p_blockweave->GetMempoolSize();
    size_t n_block_count = p_blockweave->GetBlockCount();
    size_t n_orphan_blocks_size = p_blockweave->GetOrphanBlocksSize();
    bool f_mining = p_blockweave->IsMiningEnabled();

    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"blocks\": " << n_block_count << ",\n";
    oss << "  \"mempool_size\": " << n_mempool_size << ",\n";
    oss << "  \"orphan_blocks_size\": " << n_orphan_blocks_size << ",\n";
    oss << "  \"mining_enabled\": " << (f_mining ? "true" : "false") << "\n";
    oss << "}";

    return {HTTP_OK, oss.str()};
}

std::tuple<int, std::string> CRestApiServer::HandleGetTransaction(const std::string& str_endpoint) {
    LOG_INFO("HandleGetTransaction: " + str_endpoint);

    try {
        // Parse query parameter: /transaction?hash=<hash>
        size_t n_query_pos = str_endpoint.find('?');
        if (n_query_pos == std::string::npos) {
            LOG_ERROR("GET /transaction: Missing query parameter 'hash'");
            return {HTTP_BAD_REQUEST,
                    "{\"error\": \"Bad Request\", \"message\": \"Missing query parameter 'hash'\"}"};
        }

        std::string str_query = str_endpoint.substr(n_query_pos + 1);
        std::string str_hash_param = "hash=";

        if (str_query.substr(0, 5) != str_hash_param) {
            LOG_ERROR("GET /transaction: Invalid query parameter");
            return {HTTP_BAD_REQUEST,
                    "{\"error\": \"Bad Request\", \"message\": \"Expected query parameter 'hash'\"}"};
        }

        std::string str_tx_hash = str_query.substr(5);

        if (str_tx_hash.empty()) {
            LOG_ERROR("GET /transaction: Empty hash value");
            return {HTTP_BAD_REQUEST,
                    "{\"error\": \"Bad Request\", \"message\": \"Hash value cannot be empty\"}"};
        }

        // Create CHash from hex string using repurposed constructor
        try {
            CHash tx_hash(str_tx_hash);

            // Try to get transaction data (scans all blocks)
            std::vector<uint8_t> tx_data = p_blockweave->GetData(tx_hash);

            if (tx_data.empty()) {
                LOG_WARN("GET /transaction: Transaction not found: " + str_tx_hash);
                return {HTTP_NOT_FOUND,
                        "{\"error\": \"Not Found\", \"message\": \"Transaction not found\"}"};
            }

            // Convert binary data to hex string
            std::ostringstream hex_stream;
            for (uint8_t byte : tx_data) {
                hex_stream << std::hex << std::setw(2) << std::setfill('0')
                           << static_cast<int>(byte);
            }
            std::string str_data_hex = hex_stream.str();

            // Build JSON response
            std::ostringstream oss;
            oss << "{\n";
            oss << "  \"status\": \"success\",\n";
            oss << "  \"transaction_hash\": \"" << str_tx_hash << "\",\n";
            oss << "  \"data_hex\": \"" << str_data_hex << "\",\n";
            oss << "  \"data_size\": " << tx_data.size() << "\n";
            oss << "}";

            LOG_INFO("Transaction retrieved: " + str_tx_hash);
            return {HTTP_OK, oss.str()};

        } catch (const std::invalid_argument& e) {
            LOG_ERROR("GET /transaction: Invalid hex string: " + std::string(e.what()));
            return {HTTP_BAD_REQUEST,
                    "{\"error\": \"Bad Request\", \"message\": \"" + std::string(e.what()) + "\"}"};
        }

    } catch (const std::exception& e) {
        LOG_ERROR("GET /transaction: Exception: " + std::string(e.what()));
        return {HTTP_BAD_REQUEST,
                "{\"error\": \"Bad Request\", \"message\": \"Invalid hash format\"}"};
    }
}

std::tuple<int, std::string> CRestApiServer::HandleGetBlock(const std::string& str_endpoint) {
    LOG_INFO("HandleGetBlock: " + str_endpoint);

    try {
        // Parse query parameter: /block?hash=<hash>
        size_t n_query_pos = str_endpoint.find('?');
        if (n_query_pos == std::string::npos) {
            LOG_ERROR("GET /block: Missing query parameter 'hash'");
            return {HTTP_BAD_REQUEST,
                    "{\"error\": \"Bad Request\", \"message\": \"Missing query parameter 'hash'\"}"};
        }

        std::string str_query = str_endpoint.substr(n_query_pos + 1);
        std::string str_hash_param = "hash=";

        if (str_query.substr(0, 5) != str_hash_param) {
            LOG_ERROR("GET /block: Invalid query parameter");
            return {HTTP_BAD_REQUEST,
                    "{\"error\": \"Bad Request\", \"message\": \"Expected query parameter 'hash'\"}"};
        }

        std::string str_block_hash = str_query.substr(5);

        if (str_block_hash.empty()) {
            LOG_ERROR("GET /block: Empty hash value");
            return {HTTP_BAD_REQUEST,
                    "{\"error\": \"Bad Request\", \"message\": \"Hash value cannot be empty\"}"};
        }

        // Create CHash from hex string using repurposed constructor
        try {
            CHash block_hash(str_block_hash);

            // Get block from blockweave (checks memory, then disk)
            auto p_block = p_blockweave->GetBlock(block_hash);

            if (!p_block) {
                LOG_WARN("GET /block: Block not found: " + str_block_hash);
                return {HTTP_NOT_FOUND,
                        "{\"error\": \"Not Found\", \"message\": \"Block not found\"}"};
            }

            // Build JSON response with block information
            std::ostringstream oss;
            oss << "{\n";
            oss << "  \"status\": \"success\",\n";
            oss << "  \"block_hash\": \"" << str_block_hash << "\",\n";
            oss << "  \"height\": " << p_block->GetHeight() << ",\n";
            oss << "  \"timestamp\": " << p_block->GetTimestamp() << ",\n";
            oss << "  \"nonce\": " << p_block->GetNonce() << ",\n";
            oss << "  \"transaction_count\": " << p_block->GetTransactions().size() << ",\n";
            oss << "  \"miner\": \"" << p_block->GetMiner() << "\",\n";
            oss << "  \"difficulty\": " << p_block->GetDifficulty() << ",\n";
            oss << "  \"cumulative_data_size\": " << p_block->GetCumulativeDataSize() << ",\n";
            oss << "  \"previous_block\": \"" << p_block->GetPreviousBlock().GetData() << "\"\n";
            oss << "}";

            LOG_INFO("Block retrieved: " + str_block_hash +
                     " (height: " + std::to_string(p_block->GetHeight()) + ")");
            return {HTTP_OK, oss.str()};

        } catch (const std::invalid_argument& e) {
            LOG_ERROR("GET /block: Invalid hex string: " + std::string(e.what()));
            return {HTTP_BAD_REQUEST,
                    "{\"error\": \"Bad Request\", \"message\": \"" + std::string(e.what()) + "\"}"};
        }

    } catch (const std::exception& e) {
        LOG_ERROR("GET /block: Exception: " + std::string(e.what()));
        return {HTTP_BAD_REQUEST,
                "{\"error\": \"Bad Request\", \"message\": \"Invalid hash format\"}"};
    }
}

std::tuple<int, std::string> CRestApiServer::HandlePostTransaction(const std::string& str_body) {
    LOG_INFO("HandlePostTransaction: " + str_body);
    try {
        // Parse JSON body
        std::string str_from = ExtractJsonValue(str_body, "from");
        std::string str_to = ExtractJsonValue(str_body, "to");
        std::string str_data = ExtractJsonValue(str_body, "data");
        std::string str_fee = ExtractJsonValue(str_body, "fee");

        // Validate required fields: from, to, data
        if (str_from.empty() || str_to.empty() || str_data.empty()) {
            LOG_ERROR("POST /transaction: Missing required field 'from', 'to', or 'data'");
            return {HTTP_BAD_REQUEST, "{\"error\": \"Bad Request\", \"message\": \"Missing required field: from, to, data\"}"};
        }

        // Parse fee (optional, default to 0)
        uint64_t n_fee = 0;
        if (!str_fee.empty()) {
            try {
                n_fee = std::stoull(str_fee);
            } catch (const std::exception&) {
                LOG_ERROR("POST /transaction: Invalid fee value: " + str_fee);
                return {HTTP_BAD_REQUEST, "{\"error\": \"Bad Request\", \"message\": \"Invalid fee value\"}"};
            }
        }

        // For now, treat data as plain text (not base64)
        std::vector<uint8_t> data(str_data.begin(), str_data.end());

        // Create transaction with provided addresses and fee
        auto tx = std::make_shared<CTransaction>(str_from, str_to, data, n_fee);

        // Add to mempool
        p_blockweave->AddTransaction(tx);

        // Build response
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"status\": \"success\",\n";
        oss << "  \"transaction_id\": \"" << tx->m_id.GetData() << "...\",\n";
        oss << "  \"from\": \"" << str_from << "\",\n";
        oss << "  \"to\": \"" << str_to << "\",\n";
        oss << "  \"data_size\": " << data.size() << ",\n";
        oss << "  \"fee\": " << n_fee << "\n";
        oss << "}";

        LOG_INFO("Transaction created: " + tx->m_id.GetData() + "... (from: " +
                 str_from + ", to: " + str_to + ", size: " + std::to_string(data.size()) +
                 " bytes, fee: " + std::to_string(n_fee) + ")");

        return {HTTP_OK, oss.str()};
    } catch (const std::exception& e) {
        LOG_ERROR("POST /transaction exception: " + std::string(e.what()));
        return {HTTP_INTERNAL_SERVER_ERROR, "{\"error\": \"Internal Server Error\", \"message\": \"" + std::string(e.what()) + "\"}"};
    }
}

std::tuple<int, std::string> CRestApiServer::HandleRpcAddPeer(const std::string& str_body) {
    LOG_INFO("HandleRpcAddPeer: " + str_body);
    try {
        // Parse JSON body to extract address and port
        std::string str_address = ExtractJsonValue(str_body, "address");
        std::string str_port = ExtractJsonValue(str_body, "port");

        // Validate required fields
        if (str_address.empty()) {
            LOG_ERROR("POST /rpc/addpeer: Missing required field 'address'");
            return {HTTP_BAD_REQUEST, "{\"error\": \"Bad Request\", \"message\": \"Missing required field: address\"}"};
        }

        // Parse port (optional, default to P2P_PORT)
        int n_peer_port = P2P_PORT;
        if (!str_port.empty()) {
            try {
                n_peer_port = std::stoi(str_port);
                if (n_peer_port <= 0 || n_peer_port > 65535) {
                    LOG_ERROR("POST /rpc/addpeer: Invalid port value: " + str_port);
                    return {HTTP_BAD_REQUEST, "{\"error\": \"Bad Request\", \"message\": \"Invalid port value (must be 1-65535)\"}"};
                }
            } catch (const std::exception&) {
                LOG_ERROR("POST /rpc/addpeer: Invalid port value: " + str_port);
                return {HTTP_BAD_REQUEST, "{\"error\": \"Bad Request\", \"message\": \"Invalid port value\"}"};
            }
        }

        // Add peer via peer manager
        bool f_success = p_peer_manager->AddPeer(str_address, n_peer_port);

        // Build response
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"status\": \"" << (f_success ? "success" : "failed") << "\",\n";
        oss << "  \"address\": \"" << str_address << "\",\n";
        oss << "  \"port\": " << n_peer_port << ",\n";
        oss << "  \"message\": \"" << (f_success ? "Peer connection initiated" : "Failed to initiate peer connection") << "\"\n";
        oss << "}";

        LOG_INFO("RPC addpeer: " + str_address + ":" + std::to_string(n_peer_port) + " - " +
                 (f_success ? "successfully sent, waiting for confirmation." : "failed to send"));

        return {HTTP_OK, oss.str()};
    } catch (const std::exception& e) {
        LOG_ERROR("POST /rpc/addpeer exception: " + std::string(e.what()));
        return {HTTP_INTERNAL_SERVER_ERROR, "{\"error\": \"Internal Server Error\", \"message\": \"" + std::string(e.what()) + "\"}"};
    }
}

std::tuple<int, std::string> CRestApiServer::HandleRpcPing() {
    LOG_INFO("HandleRpcPing");
    try {
        // Send PING to all connected peers immediately via peer manager
        size_t n_peers_sent = p_peer_manager->SendPingToAllPeers();

        // Build JSON response
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"status\": \"success\",\n";
        oss << "  \"message\": \"PING sent to all connected peers\",\n";
        oss << "  \"peers_count\": " << n_peers_sent << "\n";
        oss << "}";

        LOG_INFO("RPC ping: Sent PING to " + std::to_string(n_peers_sent) + " peers");

        return {HTTP_OK, oss.str()};
    } catch (const std::exception& e) {
        LOG_ERROR("POST /rpc/ping exception: " + std::string(e.what()));
        return {HTTP_INTERNAL_SERVER_ERROR, "{\"error\": \"Internal Server Error\", \"message\": \"" + std::string(e.what()) + "\"}"};
    }
}

std::tuple<int, std::string> CRestApiServer::HandleRpcGetPeer() {
    LOG_INFO("HandleRpcGetPeer");
    try {
        // Get connected peers from peer manager
        std::vector<std::shared_ptr<CPeerNode>> vec_peers = p_peer_manager->GetConnectedPeers();
        size_t n_outbound_count = p_peer_manager->GetOutboundPeerCount();
        size_t n_inbound_count = p_peer_manager->GetInboundPeerCount();

        // Build JSON response with peer list
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"status\": \"success\",\n";
        oss << "  \"total_peers\": " << vec_peers.size() << ",\n";
        oss << "  \"outbound_peers\": " << n_outbound_count << ",\n";
        oss << "  \"inbound_peers\": " << n_inbound_count << ",\n";
        oss << "  \"peers\": [\n";

        for (size_t i = 0; i < vec_peers.size(); i++) {
            // Use GetInfo() to get peer information as JSON
            std::string str_peer_info = vec_peers[i]->GetInfo();
            // Indent the peer info by 4 spaces
            size_t pos = 0;
            while ((pos = str_peer_info.find("\n", pos)) != std::string::npos) {
                str_peer_info.replace(pos, 1, "\n    ");
                pos += 5;
            }
            oss << "    " << str_peer_info;
            if (i < vec_peers.size() - 1) {
                oss << ",";
            }
            oss << "\n";
        }

        oss << "  ]\n";
        oss << "}";

        LOG_INFO("RPC getpeer: " + std::to_string(vec_peers.size()) + " peers connected");

        return {HTTP_OK, oss.str()};
    } catch (const std::exception& e) {
        LOG_ERROR("POST /rpc/getpeer exception: " + std::string(e.what()));
        return {HTTP_INTERNAL_SERVER_ERROR, "{\"error\": \"Internal Server Error\", \"message\": \"" + std::string(e.what()) + "\"}"};
    }
}

std::tuple<int, std::string> CRestApiServer::HandleRpcMineTrigger() {
    LOG_INFO("HandleRpcMineTrigger");
    try {
        // Check if we're on localnet
        std::string str_network = p_config->GetNetwork();
        if (str_network != "localnet") {
            LOG_WARN("Mining control attempted on non-localnet network: " + str_network);
            return {HTTP_FORBIDDEN,
                    "{\"error\": \"Forbidden\", \"message\": \"Mining control is only available on localnet. Current network: " + str_network + "\"}"};
        }

        // Mine one block immediately
        p_blockweave->MineBlock(str_miner_address);

        // Build JSON response with block info
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"status\": \"success\",\n";
        oss << "  \"message\": \"Block mined successfully\",\n";
        oss << "  \"block_height\": " << p_blockweave->GetBlockCount() - 1 << "\n";
        oss << "}";

        LOG_INFO("RPC minetrigger: Block mined successfully at height " + std::to_string(p_blockweave->GetBlockCount() - 1));

        return {HTTP_OK, oss.str()};
    } catch (const std::exception& e) {
        LOG_ERROR("POST /rpc/minetrigger exception: " + std::string(e.what()));
        return {HTTP_INTERNAL_SERVER_ERROR, "{\"error\": \"Internal Server Error\", \"message\": \"" + std::string(e.what()) + "\"}"};
    }
}

std::tuple<int, std::string> CRestApiServer::HandleRpcSetMockTime(const std::string& str_body) {
    try {
        // Check network - only allow on localnet
        std::string str_network = p_config->GetNetwork();
        if (str_network != "localnet") {
            LOG_ERROR("POST /rpc/setmocktime: Only allowed on localnet (current: " + str_network + ")");
            return {HTTP_FORBIDDEN, "{\"error\": \"setmocktime only available on localnet\"}"};
        }

        // Parse JSON body for "time" field
        std::string str_time = ExtractJsonValue(str_body, "time");
        if (str_time.empty()) {
            LOG_ERROR("POST /rpc/setmocktime: Missing required field 'time'");
            return {HTTP_BAD_REQUEST, "{\"error\": \"Missing required field 'time'\"}"};
        }

        // Parse time value
        int64_t n_mock_time = 0;
        try {
            n_mock_time = std::stoll(str_time);
        } catch (...) {
            LOG_ERROR("POST /rpc/setmocktime: Invalid time value: " + str_time);
            return {HTTP_BAD_REQUEST, "{\"error\": \"Invalid time value\"}"};
        }

        // Set mock time
        TimeUtil::SetMockTime(n_mock_time);

        LOG_INFO("Mock time set to: " + std::to_string(n_mock_time) +
                 (n_mock_time == 0 ? " (disabled)" : ""));

        std::ostringstream oss;
        oss << "{"
            << "\"result\": \"success\", "
            << "\"mocktime\": " << n_mock_time << ", "
            << "\"enabled\": " << (n_mock_time != 0 ? "true" : "false")
            << "}";

        return {HTTP_OK, oss.str()};

    } catch (const std::exception& e) {
        LOG_ERROR("POST /rpc/setmocktime exception: " + std::string(e.what()));
        return {HTTP_INTERNAL_SERVER_ERROR, "{\"error\": \"Internal server error\"}"};
    }
}

std::tuple<int, std::string> CRestApiServer::HandleRpcTriggerRotation() {
    try {
        // Check network - only allow on localnet
        std::string str_network = p_config->GetNetwork();
        if (str_network != "localnet") {
            LOG_ERROR("POST /rpc/triggerrotation: Only allowed on localnet (current: " + str_network + ")");
            return {HTTP_FORBIDDEN, "{\"error\": \"triggerrotation only available on localnet\"}"};
        }

        // Trigger rotation check immediately
        p_peer_manager->RotateOutboundConnections();

        LOG_INFO("Manual rotation check triggered");

        return {HTTP_OK, "{\"result\": \"success\", \"message\": \"Rotation check executed\"}"};

    } catch (const std::exception& e) {
        LOG_ERROR("POST /rpc/triggerrotation exception: " + std::string(e.what()));
        return {HTTP_INTERNAL_SERVER_ERROR, "{\"error\": \"Internal server error\"}"};
    }
}

std::tuple<int, std::string> CRestApiServer::HandleRpcDisconnectPeer(const std::string& str_body) {
    try {
        // Check network - only allow on localnet
        std::string str_network = p_config->GetNetwork();
        if (str_network != "localnet") {
            LOG_ERROR("POST /rpc/disconnectpeer: Only allowed on localnet (current: " + str_network + ")");
            return {HTTP_FORBIDDEN, "{\"error\": \"disconnectpeer only available on localnet\"}"};
        }

        // Parse JSON body for "address" and "port" fields
        std::string str_address = ExtractJsonValue(str_body, "address");
        std::string str_port = ExtractJsonValue(str_body, "port");

        if (str_address.empty() || str_port.empty()) {
            LOG_ERROR("POST /rpc/disconnectpeer: Missing required fields 'address' or 'port'");
            return {HTTP_BAD_REQUEST, "{\"error\": \"Missing required fields 'address' or 'port'\"}"};
        }

        // Parse port value
        int n_peer_port = 0;
        try {
            n_peer_port = std::stoi(str_port);
        } catch (...) {
            LOG_ERROR("POST /rpc/disconnectpeer: Invalid port value: " + str_port);
            return {HTTP_BAD_REQUEST, "{\"error\": \"Invalid port value\"}"};
        }

        // Disconnect the peer
        bool f_success = p_peer_manager->DisconnectPeerByAddress(str_address, n_peer_port);

        if (f_success) {
            LOG_INFO("Disconnected peer: " + str_address + ":" + std::to_string(n_peer_port));
            return {HTTP_OK, "{\"result\": \"success\", \"message\": \"Peer disconnected\"}"};
        } else {
            LOG_WARN("Peer not found: " + str_address + ":" + std::to_string(n_peer_port));
            return {HTTP_NOT_FOUND, "{\"error\": \"Peer not found\"}"};
        }

    } catch (const std::exception& e) {
        LOG_ERROR("POST /rpc/disconnectpeer exception: " + std::string(e.what()));
        return {HTTP_INTERNAL_SERVER_ERROR, "{\"error\": \"Internal server error\"}"};
    }
}

// ============= HTTP Method Handlers (Interface Implementation) =============

std::tuple<int, std::string> CRestApiServer::HandleGET(const std::string& str_endpoint) {
    LOG_INFO("Handling GET request for endpoint: " + str_endpoint);

    // Route based on endpoint
    if (str_endpoint == "/chain") {
        return HandleGetChain();
    }
    else if (str_endpoint == "/rpc/getpeer") {
        return HandleRpcGetPeer();
    }
    else if (str_endpoint.substr(0, 12) == "/transaction") {
        return HandleGetTransaction(str_endpoint);
    }
    else if (str_endpoint.substr(0, 6) == "/block") {
        return HandleGetBlock(str_endpoint);
    }
    else {
        LOG_ERROR("GET endpoint not found: " + str_endpoint);
        return {HTTP_NOT_FOUND, "{\"error\": \"Not found\"}"};
    }
}

std::tuple<int, std::string> CRestApiServer::HandlePOST(const std::string& str_endpoint, const CHttpRequest& request) {
    LOG_INFO("Handling POST request for endpoint: " + str_endpoint);

    // Route based on endpoint
    if (str_endpoint == "/rpc/transaction") {
        return HandlePostTransaction(request.str_body);
    }
    else if (str_endpoint == "/rpc/addpeer") {
        return HandleRpcAddPeer(request.str_body);
    }
    else if (str_endpoint == "/rpc/ping") {
        return HandleRpcPing();
    }
    else if (str_endpoint == "/rpc/minetrigger") {
        return HandleRpcMineTrigger();
    }
    else if (str_endpoint == "/rpc/setmocktime") {
        return HandleRpcSetMockTime(request.str_body);
    }
    else if (str_endpoint == "/rpc/triggerrotation") {
        return HandleRpcTriggerRotation();
    }
    else if (str_endpoint == "/rpc/disconnectpeer") {
        return HandleRpcDisconnectPeer(request.str_body);
    }
    else {
        LOG_ERROR("POST endpoint not found: " + str_endpoint);
        return {HTTP_NOT_FOUND, "{\"error\": \"Not found\"}"};
    }
}
