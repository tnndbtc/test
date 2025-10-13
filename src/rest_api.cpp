// ============= rest_api.cpp =============
#include "rest_api.h"
#include "logger/logger.h"
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>

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

CRestApiServer::CRestApiServer(CBlockweave* p_weave, const std::string& str_miner_addr,
                               int n_port_num, int n_num_workers)
    : p_blockweave(p_weave), str_miner_address(str_miner_addr), n_port(n_port_num),
      n_server_socket(-1), f_running(false), f_stop_requested(false),
      p_request_queue(std::make_shared<CRequestQueue>()) {

    m_worker_threads.reserve(n_num_workers);
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
    for (size_t n_i = 0; n_i < 5; n_i++) {
        m_worker_threads.emplace_back(&CRestApiServer::WorkerThread, this, n_i);
    }

    std::cout << "[REST API] Server started on port " << n_port << "\n";
    std::cout << "[REST API] Worker threads: 5\n";
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

    // Skip headers
    while (std::getline(iss, str_line) && str_line != "\r" && !str_line.empty()) {}

    // Read body
    std::string str_body_line;
    while (std::getline(iss, str_body_line)) {
        request.str_body += str_body_line;
    }

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

    if (request.str_method == "GET" && request.str_path == "/chain") {
        str_response = HandleGetChain();
        SendHttpResponse(request.n_client_socket, 200, "application/json", str_response);
        LOG_INFO("Handled GET /chain request");
    }
    else if (request.str_method == "POST" && request.str_path == "/mine/start") {
        str_response = HandlePostMineStart();
        SendHttpResponse(request.n_client_socket, 200, "application/json", str_response);
        LOG_INFO("Handled POST /mine/start request - mining started");
    }
    else if (request.str_method == "POST" && request.str_path == "/mine/stop") {
        str_response = HandlePostMineStop();
        SendHttpResponse(request.n_client_socket, 200, "application/json", str_response);
        LOG_INFO("Handled POST /mine/stop request - mining stopped");
    }
    else {
        str_response = "{\"error\": \"Not found\"}";
        SendHttpResponse(request.n_client_socket, 404, "application/json", str_response);
        LOG_ERROR("Unknown endpoint: " + request.str_method + " " + request.str_path);
    }
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
    // TODO: Implement transaction creation
    return "{\"error\": \"Not implemented\"}";
}

std::string CRestApiServer::HandlePostMineStart() {
    p_blockweave->StartMining();
    return "{\"status\": \"Mining started\"}";
}

std::string CRestApiServer::HandlePostMineStop() {
    p_blockweave->StopMining();
    return "{\"status\": \"Mining stopped\"}";
}
