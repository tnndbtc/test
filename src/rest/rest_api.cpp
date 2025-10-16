// ============= rest_api.cpp =============
#include "rest_api.h"
#include "cli/config.h"
#include "logger/logger.h"
#include "transaction.h"
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

// Simple JSON value extractor (extracts value for a given key)
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

// Base64 decoding table
static const std::string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

static bool IsBase64(unsigned char c) {
    return (std::isalnum(c) || (c == '+') || (c == '/'));
}

// Decode base64 string to bytes
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

// Extract filename and file data from multipart form-data
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

// Generate UUID v4 string
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

// Create directory recursively
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

CRequestQueue::CRequestQueue() : f_shutdown(false) {}

void CRequestQueue::Enqueue(const CHttpRequest& request) {
    std::lock_guard<std::mutex> lock(cs_queue);
    m_queue.push(request);
    cv_queue.notify_one();
}

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

void CRequestQueue::Shutdown() {
    f_shutdown = true;
    cv_queue.notify_all();
}

size_t CRequestQueue::Size() const {
    std::lock_guard<std::mutex> lock(cs_queue);
    return m_queue.size();
}

// ============= CRestApiServer Implementation =============

CRestApiServer::CRestApiServer(CBlockweave* p_weave, const CConfig* p_cfg,
                               const std::string& str_miner_addr,
                               int n_port_num)
    : p_blockweave(p_weave), p_config(p_cfg), str_miner_address(str_miner_addr), n_port(n_port_num),
      n_server_socket(-1), f_running(false), f_stop_requested(false),
      p_request_queue(std::make_shared<CRequestQueue>()) {

    m_worker_threads.reserve(WORKER_THREADS);
}

CRestApiServer::~CRestApiServer() {
    Stop();
}

bool CRestApiServer::Start() {
    LOG_INFO("Creating REST API server socket");

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
    LOG_INFO("REST API server bound to port " + std::to_string(n_port));

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
    for (size_t n_i = 0; n_i < WORKER_THREADS; n_i++) {
        m_worker_threads.emplace_back(&CRestApiServer::WorkerThread, this, n_i);
    }

    std::cout << "[REST API] Server started on port " << n_port << "\n";
    std::cout << "[REST API] Worker threads: " << WORKER_THREADS << "\n";
    LOG_INFO("REST API listener thread and worker threads started");

    return true;
}

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

    std::cout << "[REST API] Server stopped\n";
    LOG_INFO("REST API server stopped, all threads joined");
}

bool CRestApiServer::IsRunning() const {
    return f_running;
}

void CRestApiServer::ListenerThread() {
    std::cout << "[REST API Listener] Thread started\n";
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

    std::cout << "[REST API Listener] Thread stopped\n";
    LOG_INFO("REST API listener thread stopped");
}

void CRestApiServer::WorkerThread(int n_worker_id) {
    std::cout << "[REST API Worker " << n_worker_id << "] Thread started\n";
    LOG_INFO("REST API worker thread " + std::to_string(n_worker_id) + " started");

    while (!f_stop_requested) {
        CHttpRequest request;
        if (p_request_queue->Dequeue(request, 100)) {
            ProcessRequest(request);
            close(request.n_client_socket);
        }
    }

    std::cout << "[REST API Worker " << n_worker_id << "] Thread stopped\n";
    LOG_INFO("REST API worker thread " + std::to_string(n_worker_id) + " stopped");
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
    std::cout << "[REST API] " << request.str_method << " " << request.str_path << "\n";
    LOG_INFO("Processing request: " + request.str_method + " " + request.str_path);

    std::string str_response;
    int n_status_code = 200;

    // Route to appropriate handler based on HTTP method
    if (request.str_method == "GET") {
        str_response = HandleGET(request.str_path, request);
        if (str_response.find("\"error\"") != std::string::npos &&
            str_response.find("Not found") != std::string::npos) {
            n_status_code = 404;
        }
    }
    else if (request.str_method == "POST") {
        str_response = HandlePOST(request.str_path, request);
        if (str_response.find("\"error\"") != std::string::npos &&
            str_response.find("Not found") != std::string::npos) {
            n_status_code = 404;
        }
    }
    else {
        str_response = "{\"error\": \"Method not allowed\"}";
        n_status_code = 405;
        LOG_ERROR("Unsupported HTTP method: " + request.str_method);
    }

    SendHttpResponse(request.n_client_socket, n_status_code, "application/json", str_response);
}

std::string CRestApiServer::HandleGetChain() {
    size_t n_mempool_size = p_blockweave->GetMempoolSize();
    bool f_mining = p_blockweave->IsMiningEnabled();

    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"mempool_size\": " << n_mempool_size << ",\n";
    oss << "  \"mining_enabled\": " << (f_mining ? "true" : "false") << "\n";
    oss << "}";

    return oss.str();
}

std::string CRestApiServer::HandleGetBlock(const std::string& str_hash) {
    // TODO: Implement block retrieval
    return "{\"error\": \"Not implemented\"}";
}

std::string CRestApiServer::HandleGetData(const std::string& str_tx_id) {
    // TODO: Implement data retrieval
    return "{\"error\": \"Not implemented\"}";
}

std::string CRestApiServer::HandlePostTransaction(const std::string& str_body) {
    try {
        // Parse JSON body
        std::string str_from = ExtractJsonValue(str_body, "from");
        std::string str_to = ExtractJsonValue(str_body, "to");
        std::string str_data_b64 = ExtractJsonValue(str_body, "data");
        std::string str_fee = ExtractJsonValue(str_body, "fee");

        // Validate required fields
        if (str_from.empty() || str_to.empty() || str_data_b64.empty()) {
            LOG_ERROR("POST /transaction: Missing required fields (from, to, or data)");
            return "{\"error\": \"Missing required fields: from, to, data\"}";
        }

        // Decode base64 data
        std::vector<uint8_t> data = DecodeBase64(str_data_b64);
        if (data.empty()) {
            LOG_ERROR("POST /transaction: Failed to decode base64 data");
            return "{\"error\": \"Invalid base64 data\"}";
        }

        // Parse fee (default to 0 if not provided or invalid)
        uint64_t n_fee = 0;
        if (!str_fee.empty()) {
            try {
                n_fee = static_cast<uint64_t>(std::stod(str_fee) * 1000000); // Convert to smallest unit
            } catch (...) {
                LOG_ERROR("POST /transaction: Invalid fee value: " + str_fee);
                return "{\"error\": \"Invalid fee value\"}";
            }
        }

        // Create transaction
        auto tx = std::make_shared<CTransaction>(str_from, str_to, data, n_fee);

        // Add to mempool
        p_blockweave->AddTransaction(tx);

        // Build response
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"status\": \"success\",\n";
        oss << "  \"transaction_id\": \"" << tx->m_id.m_str_data.substr(0, 32) << "...\",\n";
        oss << "  \"from\": \"" << str_from.substr(0, 16) << "...\",\n";
        oss << "  \"to\": \"" << str_to.substr(0, 16) << "...\",\n";
        oss << "  \"data_size\": " << data.size() << ",\n";
        oss << "  \"fee\": " << n_fee << "\n";
        oss << "}";

        LOG_INFO("Transaction created: " + tx->m_id.m_str_data.substr(0, 16) + "... (from: " +
                 str_from.substr(0, 16) + "..., to: " + str_to.substr(0, 16) + "..., size: " +
                 std::to_string(data.size()) + " bytes)");

        return oss.str();
    } catch (const std::exception& e) {
        LOG_ERROR("POST /transaction exception: " + std::string(e.what()));
        return "{\"error\": \"Internal server error\"}";
    }
}

std::string CRestApiServer::HandlePostFiles(const CHttpRequest& request) {
    try {
        std::string str_filename;
        std::vector<uint8_t> file_data;

        // Check Content-Type to determine how to parse
        if (request.str_content_type.find("multipart/form-data") != std::string::npos) {
            // Extract boundary from Content-Type
            size_t n_boundary_pos = request.str_content_type.find("boundary=");
            if (n_boundary_pos == std::string::npos) {
                LOG_ERROR("POST /files: Missing boundary in multipart/form-data");
                return "{\"error\": \"Missing boundary in Content-Type\"}";
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
                return "{\"error\": \"Failed to parse multipart data\"}";
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
            return "{\"error\": \"Empty file data\"}";
        }

        // Generate UUID for file name
        std::string str_uuid = GenerateUUID();

        // Get data directory from config
        std::string str_data_dir = p_config->GetDataDir();

        // Create data directory if it doesn't exist
        if (!CreateDirectoryRecursive(str_data_dir)) {
            LOG_ERROR("POST /files: Failed to create data directory: " + str_data_dir);
            return "{\"error\": \"Failed to create data directory\"}";
        }

        // Build full file path
        std::string str_file_path = str_data_dir + "/" + str_uuid;

        // Save file to disk
        std::ofstream file(str_file_path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR("POST /files: Failed to open file for writing: " + str_file_path);
            return "{\"error\": \"Failed to save file\"}";
        }

        file.write(reinterpret_cast<const char*>(file_data.data()), file_data.size());
        file.close();

        if (file.fail()) {
            LOG_ERROR("POST /files: Failed to write file data to: " + str_file_path);
            return "{\"error\": \"Failed to write file\"}";
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
        oss << "  \"transaction_id\": \"" << tx->m_id.m_str_data.substr(0, 32) << "...\",\n";
        oss << "  \"uuid\": \"" << str_uuid << "\",\n";
        oss << "  \"original_filename\": \"" << str_filename << "\",\n";
        oss << "  \"saved_path\": \"" << str_file_path << "\",\n";
        oss << "  \"size\": " << file_data.size() << ",\n";
        oss << "  \"message\": \"File uploaded and saved to disk\"\n";
        oss << "}";

        LOG_INFO("File uploaded: " + str_filename + " -> " + str_uuid + " (" +
                 std::to_string(file_data.size()) + " bytes, TX: " +
                 tx->m_id.m_str_data.substr(0, 16) + "...)");

        return oss.str();
    } catch (const std::exception& e) {
        LOG_ERROR("POST /files exception: " + std::string(e.what()));
        return "{\"error\": \"Internal server error\"}";
    }
}

std::string CRestApiServer::HandlePostMineStart() {
    p_blockweave->StartMining();
    return "{\"status\": \"Mining started\"}";
}

std::string CRestApiServer::HandlePostMineStop() {
    p_blockweave->StopMining();
    return "{\"status\": \"Mining stopped\"}";
}

// ============= HTTP Method Handlers (Interface Implementation) =============

std::string CRestApiServer::HandleGET(const std::string& str_endpoint, const CHttpRequest& request) {
    LOG_INFO("Handling GET request for endpoint: " + str_endpoint);

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
        return "{\"error\": \"Not found\"}";
    }
}

std::string CRestApiServer::HandlePOST(const std::string& str_endpoint, const CHttpRequest& request) {
    LOG_INFO("Handling POST request for endpoint: " + str_endpoint);

    // Route based on endpoint
    if (str_endpoint == "/transaction") {
        return HandlePostTransaction(request.str_body);
    }
    else if (str_endpoint == "/files") {
        return HandlePostFiles(request);
    }
    else if (str_endpoint == "/mine/start") {
        return HandlePostMineStart();
    }
    else if (str_endpoint == "/mine/stop") {
        return HandlePostMineStop();
    }
    else {
        LOG_ERROR("POST endpoint not found: " + str_endpoint);
        return "{\"error\": \"Not found\"}";
    }
}
