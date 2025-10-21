// ============= rest_api.cpp =============
/**
 * @file rest_api.cpp
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

#include "rest_api.h"
#include "utils/config.h"
#include "utils/threadname.h"
#include "utils/httpcode.h"
#include "logger/logger.h"
#include "blockcore/transaction.h"
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <random>
#include <iomanip>
#include <sys/stat.h>

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
    int n_in_len = str_encoded.size();
    int n_i = 0;
    int n_j = 0;
    int n_in = 0;
    unsigned char char_array_4[4], char_array_3[3];

    while (n_in_len-- && (str_encoded[n_in] != '=') && IsBase64(str_encoded[n_in])) {
        char_array_4[n_i++] = str_encoded[n_in];
        n_in++;
        if (n_i == 4) {
            for (n_i = 0; n_i < 4; n_i++) {
                char_array_4[n_i] = base64_chars.find(char_array_4[n_i]);
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
            char_array_4[n_j] = base64_chars.find(char_array_4[n_j]);
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

/**
 * @brief Create directory recursively (like mkdir -p)
 * @param str_path Directory path to create
 * @return true if directory exists or was created successfully
 *
 * Creates directory and all parent directories as needed.
 * Uses Unix permissions 0755 (rwxr-xr-x).
 * Returns true if directory already exists.
 */
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
    return mkdir(str_path.c_str(), 0755) == 0;
}

// ============= CRequestQueue Implementation =============

/**
 * @brief Constructor - initializes empty queue
 */
CRequestQueue::CRequestQueue() : f_shutdown(false) {}

/**
 * @brief Add request to queue and notify waiting worker
 * @param request HTTP request to enqueue
 *
 * Thread-safe enqueue operation:
 * 1. Acquire mutex lock
 * 2. Push request onto queue
 * 3. Notify one waiting worker thread
 */
void CRequestQueue::Enqueue(const CHttpRequest& request) {
    std::lock_guard<std::mutex> lock(cs_queue);
    m_queue.push(request);
    cv_queue.notify_one();
}

/**
 * @brief Remove and return request from queue (blocking with timeout)
 * @param request Output parameter to receive dequeued request
 * @param n_timeout_ms Maximum wait time in milliseconds
 * @return true if request dequeued, false on timeout or shutdown
 *
 * Thread-safe dequeue with condition variable wait:
 * 1. Acquire unique lock for condition variable
 * 2. Wait up to n_timeout_ms for queue to be non-empty
 * 3. Return false if timeout or shutdown flag set
 * 4. Pop and return front request if available
 */
bool CRequestQueue::Dequeue(CHttpRequest& request, int n_timeout_ms) {
    std::unique_lock<std::mutex> lock(cs_queue);

    if (!cv_queue.wait_for(lock, std::chrono::milliseconds(n_timeout_ms),
                           [this] { return !m_queue.empty() || f_shutdown; })) {
        return false;
    }

    if (f_shutdown && m_queue.empty()) {
        return false;
    }

    request = m_queue.front();
    m_queue.pop();
    return true;
}

/**
 * @brief Signal shutdown to all waiting threads
 *
 * Sets shutdown flag and notifies all waiting worker threads
 * to exit their dequeue loops.
 */
void CRequestQueue::Shutdown() {
    f_shutdown = true;
    cv_queue.notify_all();
}

/**
 * @brief Get current queue size (thread-safe)
 * @return Number of requests in queue
 */
size_t CRequestQueue::Size() const {
    std::lock_guard<std::mutex> lock(cs_queue);
    return m_queue.size();
}

// ============= CRestApiServer Implementation =============

/**
 * @brief Construct REST API server
 * @param p_weave Pointer to blockweave instance
 * @param p_cfg Pointer to configuration object
 * @param str_miner_addr Mining reward address
 * @param n_port_num HTTP server port
 *
 * Initializes server in stopped state. Reserves space for worker
 * threads (REST_WORKER_THREADS from settings.h, default 5).
 * Creates shared request queue for listener/worker coordination.
 */
CRestApiServer::CRestApiServer(CBlockweave* p_weave, const CConfig* p_cfg,
                               const std::string& str_miner_addr,
                               int n_port_num)
    : p_blockweave(p_weave), p_config(p_cfg), str_miner_address(str_miner_addr), n_port(n_port_num),
      n_server_socket(-1), f_running(false), f_stop_requested(false),
      p_request_queue(std::make_shared<CRequestQueue>()) {

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
    LOG_TRACE("Creating REST API server socket");

    // Create socket
    n_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (n_server_socket < 0) {
        std::cerr << "[REST API] Failed to create socket\n";
        LOG_ERROR("Failed to create REST API server socket");
        return false;
    }

    // Set socket options
    int n_opt = 1;
    setsockopt(n_server_socket, SOL_SOCKET, SO_REUSEADDR, &n_opt, sizeof(n_opt));

    // Bind socket
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(n_port);

    if (bind(n_server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[REST API] Failed to bind to port " << n_port << "\n";
        LOG_ERROR("Failed to bind REST API server to port " + std::to_string(n_port));
        close(n_server_socket);
        return false;
    }
    LOG_TRACE("REST API server bound to port " + std::to_string(n_port));

    // Listen
    if (listen(n_server_socket, 10) < 0) {
        std::cerr << "[REST API] Failed to listen\n";
        LOG_ERROR("Failed to listen on REST API server socket");
        close(n_server_socket);
        return false;
    }

    f_running = true;
    f_stop_requested = false;

    // Start listener thread
    m_listener_thread = std::thread(&CRestApiServer::ListenerThread, this);

    // Start worker threads
    for (size_t n_i = 0; n_i < REST_WORKER_THREADS; n_i++) {
        m_worker_threads.emplace_back(&CRestApiServer::WorkerThread, this, n_i);
    }

    LOG_TRACE("REST API server started on port " + std::to_string(n_port));
    LOG_TRACE("REST API worker threads: " + std::to_string(REST_WORKER_THREADS));

    return true;
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
    if (!f_running) return;

    LOG_INFO("Stopping REST API server");
    f_stop_requested = true;
    f_running = false;

    // Shutdown queue
    p_request_queue->Shutdown();

    // Close server socket
    if (n_server_socket >= 0) {
        close(n_server_socket);
        n_server_socket = -1;
    }

    // Join threads
    if (m_listener_thread.joinable()) {
        m_listener_thread.join();
    }

    for (auto& thread : m_worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

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
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int n_client_socket = accept(n_server_socket, (sockaddr*)&client_addr, &client_len);

        if (n_client_socket < 0) {
            if (!f_stop_requested) {
                std::cerr << "[REST API] Accept failed\n";
                LOG_ERROR("REST API accept() failed");
            }
            continue;
        }

        // Read request
        char buffer[4096] = {0};
        ssize_t n_bytes_read = recv(n_client_socket, buffer, sizeof(buffer) - 1, 0);

        if (n_bytes_read > 0) {
            buffer[n_bytes_read] = '\0';
            CHttpRequest request = ParseHttpRequest(std::string(buffer), n_client_socket);
            p_request_queue->Enqueue(request);
        } else {
            close(n_client_socket);
        }
    }

    LOG_TRACE("REST API listener thread stopped");
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

    LOG_TRACE("REST API worker thread " + std::to_string(n_worker_id) + " started");

    while (!f_stop_requested) {
        CHttpRequest request;
        if (p_request_queue->Dequeue(request, 100)) {
            ProcessRequest(request);
            close(request.n_client_socket);
        }
    }

    LOG_TRACE("REST API worker thread " + std::to_string(n_worker_id) + " stopped");
}

CHttpRequest CRestApiServer::ParseHttpRequest(const std::string& str_raw_request,
                                               int n_client_socket) {
    CHttpRequest request;
    request.n_client_socket = n_client_socket;

    std::istringstream iss(str_raw_request);
    std::string str_line;

    // Parse request line
    if (std::getline(iss, str_line)) {
        std::istringstream line_stream(str_line);
        line_stream >> request.str_method >> request.str_path;
    }

    // Parse headers
    while (std::getline(iss, str_line) && str_line != "\r" && !str_line.empty()) {
        // Remove trailing \r if present
        if (!str_line.empty() && str_line.back() == '\r') {
            str_line.pop_back();
        }

        // Look for Content-Type header
        size_t n_colon = str_line.find(':');
        if (n_colon != std::string::npos) {
            std::string str_header_name = str_line.substr(0, n_colon);
            std::string str_header_value = str_line.substr(n_colon + 1);

            // Trim whitespace
            str_header_name.erase(0, str_header_name.find_first_not_of(" \t"));
            str_header_name.erase(str_header_name.find_last_not_of(" \t") + 1);
            str_header_value.erase(0, str_header_value.find_first_not_of(" \t"));
            str_header_value.erase(str_header_value.find_last_not_of(" \t"));

            // Convert header name to lowercase for case-insensitive comparison
            std::transform(str_header_name.begin(), str_header_name.end(),
                          str_header_name.begin(), ::tolower);

            if (str_header_name == "content-type") {
                request.str_content_type = str_header_value;
            }
        }
    }

    // Read body - preserve all characters including newlines
    std::string remaining;
    std::getline(iss, remaining, '\0');  // Read everything remaining
    request.str_body = remaining;

    return request;
}

void CRestApiServer::SendHttpResponse(int n_client_socket, int n_status_code,
                                       const std::string& str_content_type,
                                       const std::string& str_body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << n_status_code << " OK\r\n";
    oss << "Content-Type: " << str_content_type << "\r\n";
    oss << "Content-Length: " << str_body.length() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << str_body;

    std::string str_response = oss.str();
    send(n_client_socket, str_response.c_str(), str_response.length(), 0);
}

void CRestApiServer::ProcessRequest(const CHttpRequest& request) {
    LOG_INFO("Processing request: " + request.str_method + " " + request.str_path);

    int n_status_code;
    std::string str_response;

    // Route to appropriate handler based on HTTP method
    if (request.str_method == "GET") {
        std::tie(n_status_code, str_response) = HandleGET(request.str_path, request);
    }
    else if (request.str_method == "POST") {
        std::tie(n_status_code, str_response) = HandlePOST(request.str_path, request);
    }
    else {
        n_status_code = HTTP_METHOD_NOT_ALLOWED;
        str_response = "{\"error\": \"Method Not Allowed\"}";
        LOG_ERROR("Unsupported HTTP method: " + request.str_method);
        SendHttpResponse(request.n_client_socket, n_status_code, "application/json", str_response);
        return;
    }

    // Log errors if status code indicates failure
    if (n_status_code >= 400) {
        LOG_ERROR("Request failed with status " + std::to_string(n_status_code) + ": " + str_response);
    }

    SendHttpResponse(request.n_client_socket, n_status_code, "application/json", str_response);
}

std::tuple<int, std::string> CRestApiServer::HandleGetChain() {
    size_t n_mempool_size = p_blockweave->GetMempoolSize();
    bool f_mining = p_blockweave->IsMiningEnabled();

    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"mempool_size\": " << n_mempool_size << ",\n";
    oss << "  \"mining_enabled\": " << (f_mining ? "true" : "false") << "\n";
    oss << "}";

    return {HTTP_OK, oss.str()};
}

std::tuple<int, std::string> CRestApiServer::HandleGetBlock(const std::string& str_hash) {
    // TODO: Implement block retrieval
    return {HTTP_NOT_IMPLEMENTED, "{\"error\": \"Not implemented\"}"};
}

std::tuple<int, std::string> CRestApiServer::HandleGetData(const std::string& str_tx_id) {
    // TODO: Implement data retrieval
    return {HTTP_NOT_IMPLEMENTED, "{\"error\": \"Not implemented\"}"};
}

std::tuple<int, std::string> CRestApiServer::HandlePostTransaction(const std::string& str_body) {
    try {
        // Parse JSON body
        std::string str_from = ExtractJsonValue(str_body, "from");
        std::string str_to = ExtractJsonValue(str_body, "to");
        std::string str_data = ExtractJsonValue(str_body, "data");
        std::string str_fee = ExtractJsonValue(str_body, "fee");

        // Validate required fields: from, to, data
        if (str_from.empty()) {
            LOG_ERROR("POST /transaction: Missing required field 'from'");
            return {HTTP_BAD_REQUEST, "{\"error\": \"Bad Request\", \"message\": \"Missing required field: from\"}"};
        }

        if (str_to.empty()) {
            LOG_ERROR("POST /transaction: Missing required field 'to'");
            return {HTTP_BAD_REQUEST, "{\"error\": \"Bad Request\", \"message\": \"Missing required field: to\"}"};
        }

        if (str_data.empty()) {
            LOG_ERROR("POST /transaction: Missing required field 'data'");
            return {HTTP_BAD_REQUEST, "{\"error\": \"Bad Request\", \"message\": \"Missing required field: data\"}"};
        }

        // Parse fee (optional, default to 0)
        uint64_t n_fee = 0;
        if (!str_fee.empty()) {
            try {
                n_fee = std::stoull(str_fee);
            } catch (const std::exception& e) {
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
        oss << "  \"transaction_id\": \"" << tx->m_id.GetData().substr(0, 32) << "...\",\n";
        oss << "  \"from\": \"" << str_from << "\",\n";
        oss << "  \"to\": \"" << str_to << "\",\n";
        oss << "  \"data_size\": " << data.size() << ",\n";
        oss << "  \"fee\": " << n_fee << "\n";
        oss << "}";

        LOG_INFO("Transaction created: " + tx->m_id.GetData().substr(0, 16) + "... (from: " +
                 str_from + ", to: " + str_to + ", size: " + std::to_string(data.size()) +
                 " bytes, fee: " + std::to_string(n_fee) + ")");

        return {HTTP_OK, oss.str()};
    } catch (const std::exception& e) {
        LOG_ERROR("POST /transaction exception: " + std::string(e.what()));
        return {HTTP_INTERNAL_SERVER_ERROR, "{\"error\": \"Internal Server Error\", \"message\": \"" + std::string(e.what()) + "\"}"};
    }
}

std::tuple<int, std::string> CRestApiServer::HandlePostFiles(const CHttpRequest& request) {
    try {
        std::string str_filename;
        std::vector<uint8_t> file_data;

        // Check Content-Type to determine how to parse
        if (request.str_content_type.find("multipart/form-data") != std::string::npos) {
            // Extract boundary from Content-Type
            size_t n_boundary_pos = request.str_content_type.find("boundary=");
            if (n_boundary_pos == std::string::npos) {
                LOG_ERROR("POST /files: Missing boundary in multipart/form-data");
                return {HTTP_BAD_REQUEST, "{\"error\": \"Missing boundary in Content-Type\"}"};
            }

            std::string str_boundary = request.str_content_type.substr(n_boundary_pos + 9);
            // Remove quotes if present
            if (!str_boundary.empty() && str_boundary.front() == '"') {
                str_boundary = str_boundary.substr(1);
            }
            if (!str_boundary.empty() && str_boundary.back() == '"') {
                str_boundary.pop_back();
            }

            // Parse multipart data
            if (!ParseMultipartFile(request.str_body, str_boundary, str_filename, file_data)) {
                LOG_ERROR("POST /files: Failed to parse multipart data");
                return {HTTP_BAD_REQUEST, "{\"error\": \"Failed to parse multipart data\"}"};
            }

            if (str_filename.empty()) {
                str_filename = "uploaded_file";
            }
        } else {
            // Raw file upload - use entire body as file data
            str_filename = "raw_upload";
            for (char c : request.str_body) {
                file_data.push_back(static_cast<uint8_t>(c));
            }
        }

        if (file_data.empty()) {
            LOG_ERROR("POST /files: Empty file data");
            return {HTTP_BAD_REQUEST, "{\"error\": \"Empty file data\"}"};
        }

        // Generate UUID for file name
        std::string str_uuid = GenerateUUID();

        // Get data directory from config
        std::string str_data_dir = p_config->GetDataDir();

        // Create data directory if it doesn't exist
        if (!CreateDirectoryRecursive(str_data_dir)) {
            LOG_ERROR("POST /files: Failed to create data directory: " + str_data_dir);
            return {HTTP_INTERNAL_SERVER_ERROR, "{\"error\": \"Failed to create data directory\"}"};
        }

        // Build full file path
        std::string str_file_path = str_data_dir + "/" + str_uuid;

        // Save file to disk
        std::ofstream file(str_file_path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR("POST /files: Failed to open file for writing: " + str_file_path);
            return {HTTP_INTERNAL_SERVER_ERROR, "{\"error\": \"Failed to save file\"}"};
        }

        file.write(reinterpret_cast<const char*>(file_data.data()), file_data.size());
        file.close();

        if (file.fail()) {
            LOG_ERROR("POST /files: Failed to write file data to: " + str_file_path);
            return {HTTP_INTERNAL_SERVER_ERROR, "{\"error\": \"Failed to write file\"}"};
        }

        // Create transaction with file data
        // Use miner address as owner and a placeholder target
        auto tx = std::make_shared<CTransaction>(
            str_miner_address,
            "file_storage",
            file_data,
            0  // No fee for file uploads
        );

        // Add to mempool
        p_blockweave->AddTransaction(tx);

        // Build response
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"status\": \"success\",\n";
        oss << "  \"transaction_id\": \"" << tx->m_id.GetData().substr(0, 32) << "...\",\n";
        oss << "  \"uuid\": \"" << str_uuid << "\",\n";
        oss << "  \"original_filename\": \"" << str_filename << "\",\n";
        oss << "  \"saved_path\": \"" << str_file_path << "\",\n";
        oss << "  \"size\": " << file_data.size() << ",\n";
        oss << "  \"message\": \"File uploaded and saved to disk\"\n";
        oss << "}";

        LOG_INFO("File uploaded: " + str_filename + " -> " + str_uuid + " (" +
                 std::to_string(file_data.size()) + " bytes, TX: " +
                 tx->m_id.GetData().substr(0, 16) + "...)");

        return {HTTP_OK, oss.str()};
    } catch (const std::exception& e) {
        LOG_ERROR("POST /files exception: " + std::string(e.what()));
        return {HTTP_INTERNAL_SERVER_ERROR, "{\"error\": \"Internal server error\"}"};
    }
}

// ============= HTTP Method Handlers (Interface Implementation) =============

std::tuple<int, std::string> CRestApiServer::HandleGET(const std::string& str_endpoint, const CHttpRequest& request) {
    LOG_TRACE("Handling GET request for endpoint: " + str_endpoint);

    // Route based on endpoint
    if (str_endpoint == "/chain") {
        return HandleGetChain();
    }
    else if (str_endpoint.find("/block/") == 0) {
        // Extract block hash from path (e.g., /block/<hash>)
        std::string str_hash = str_endpoint.substr(7);
        return HandleGetBlock(str_hash);
    }
    else if (str_endpoint.find("/data/") == 0) {
        // Extract transaction ID from path (e.g., /data/<tx_id>)
        std::string str_tx_id = str_endpoint.substr(6);
        return HandleGetData(str_tx_id);
    }
    else {
        LOG_ERROR("GET endpoint not found: " + str_endpoint);
        return {HTTP_NOT_FOUND, "{\"error\": \"Not found\"}"};
    }
}

std::tuple<int, std::string> CRestApiServer::HandlePOST(const std::string& str_endpoint, const CHttpRequest& request) {
    LOG_INFO("Handling POST request for endpoint: " + str_endpoint);

    // Route based on endpoint
    if (str_endpoint == "/transaction") {
        return HandlePostTransaction(request.str_body);
    }
    else if (str_endpoint == "/files") {
        return HandlePostFiles(request);
    }
    else {
        LOG_ERROR("POST endpoint not found: " + str_endpoint);
        return {HTTP_NOT_FOUND, "{\"error\": \"Not found\"}"};
    }
}
