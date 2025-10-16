// ============= rest_api.h =============
#ifndef REST_API_H
#define REST_API_H

#include "i_rest_api.h"
#include "blockcore/blockweave.h"
#include "utils/settings.h"
#include <string>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <memory>

// HTTP Request structure
struct CHttpRequest {
    std::string str_method;          // GET, POST, etc.
    std::string str_path;            // URL path
    std::string str_body;            // Request body
    std::string str_content_type;    // Content-Type header
    int n_client_socket;             // Client socket for response
};

// Thread-safe request queue
class CRequestQueue {
private:
    std::queue<CHttpRequest> m_queue;
    mutable std::mutex cs_queue;
    std::condition_variable cv_queue;
    std::atomic<bool> f_shutdown;

public:
    CRequestQueue();

    void Enqueue(const CHttpRequest& request);
    bool Dequeue(CHttpRequest& request, int n_timeout_ms = 1000);
    void Shutdown();
    size_t Size() const;
};

// Forward declaration
class CConfig;

// REST API Server Implementation
// Implements the IRestApiServer interface for HTTP REST API functionality
class CRestApiServer : public IRestApiServer {
private:
    CBlockweave* p_blockweave;
    const CConfig* p_config;
    std::string str_miner_address;
    int n_port;
    int n_server_socket;

    std::atomic<bool> f_running;
    std::atomic<bool> f_stop_requested;

    // Threads
    std::thread m_listener_thread;
    std::vector<std::thread> m_worker_threads;

    // Request queue
    std::shared_ptr<CRequestQueue> p_request_queue;

    // Thread functions
    void ListenerThread();
    void WorkerThread(int n_worker_id);

    // Request handling
    void ProcessRequest(const CHttpRequest& request);
    std::string HandleGetChain();
    std::string HandleGetBlock(const std::string& str_hash);
    std::string HandleGetData(const std::string& str_tx_id);
    std::string HandlePostTransaction(const std::string& str_body);
    std::string HandlePostFiles(const CHttpRequest& request);
    std::string HandlePostMineStart();
    std::string HandlePostMineStop();

    // HTTP utilities
    CHttpRequest ParseHttpRequest(const std::string& str_raw_request, int n_client_socket);
    void SendHttpResponse(int n_client_socket, int n_status_code,
                          const std::string& str_content_type,
                          const std::string& str_body);

public:
    CRestApiServer(CBlockweave* p_weave, const CConfig* p_cfg, const std::string& str_miner_addr,
                   int n_port_num = 28443);
    virtual ~CRestApiServer() override;

    // IRestApiServer interface implementation
    virtual bool Start() override;
    virtual void Stop() override;
    virtual bool IsRunning() const override;
    virtual std::string HandleGET(const std::string& str_endpoint, const CHttpRequest& request) override;
    virtual std::string HandlePOST(const std::string& str_endpoint, const CHttpRequest& request) override;
};

#endif
