// ============= rest_api.h =============
/**
 * @file rest_api.h
 * @brief Multi-threaded HTTP REST API server for blockweave
 *
 * Implements HTTP REST API with listener/worker thread architecture.
 * Listener thread accepts connections and enqueues requests, worker
 * threads process requests from queue concurrently.
 */

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

/**
 * @struct CHttpRequest
 * @brief Represents an HTTP request with all necessary data
 *
 * Contains parsed HTTP request data and client socket for sending
 * response. Instances are created by listener thread and passed to
 * worker threads via request queue.
 */
struct CHttpRequest {
    std::string str_method;          ///< HTTP method (GET, POST, etc.)
    std::string str_path;            ///< URL path (e.g., "/chain", "/transaction")
    std::string str_body;            ///< Request body content
    std::string str_content_type;    ///< Content-Type header value
    int n_client_socket;             ///< Client socket file descriptor for response
};

/**
 * @class CRequestQueue
 * @brief Thread-safe request queue with condition variable synchronization
 *
 * Producer-consumer queue where:
 * - Listener thread produces (Enqueue)
 * - Worker threads consume (Dequeue)
 *
 * Features:
 * - Mutex-protected queue for thread safety
 * - Condition variable for efficient waiting
 * - Shutdown flag for graceful termination
 * - Timeout support in Dequeue to prevent deadlock
 *
 * Example usage:
 *   CRequestQueue queue;
 *   queue.Enqueue(request);       // Listener adds request
 *   queue.Dequeue(request, 1000); // Worker waits up to 1 second
 *   queue.Shutdown();             // Signal all workers to exit
 */
class CRequestQueue {
private:
    std::queue<CHttpRequest> m_queue;           ///< Request queue (FIFO)
    mutable std::mutex cs_queue;                ///< Mutex protecting queue access
    std::condition_variable cv_queue;           ///< Condition variable for signaling
    std::atomic<bool> f_shutdown;               ///< Shutdown flag for graceful exit

public:
    /**
     * @brief Constructor - initializes empty queue
     */
    CRequestQueue();

    /**
     * @brief Add request to queue and notify waiting worker
     * @param request HTTP request to enqueue
     */
    void Enqueue(const CHttpRequest& request);

    /**
     * @brief Remove and return request from queue (blocking with timeout)
     * @param request Output parameter to receive dequeued request
     * @param n_timeout_ms Maximum wait time in milliseconds
     * @return true if request dequeued, false on timeout or shutdown
     */
    bool Dequeue(CHttpRequest& request, int n_timeout_ms = 1000);

    /**
     * @brief Signal shutdown to all waiting threads
     */
    void Shutdown();

    /**
     * @brief Get current queue size
     * @return Number of requests in queue
     */
    size_t Size() const;
};

// Forward declaration
class CConfig;

/**
 * @class CRestApiServer
 * @brief Multi-threaded HTTP REST API server for blockweave operations
 *
 * Implements IRestApiServer interface to provide HTTP REST API access to
 * blockweave functionality. Uses listener/worker thread architecture for
 * concurrent request processing.
 *
 * Architecture:
 * - 1 listener thread accepts incoming TCP connections
 * - N worker threads (WORKER_THREADS from settings.h, default 5) process requests
 * - Thread-safe request queue coordinates between listener and workers
 * - All threads run until Stop() is called
 *
 * Supported endpoints:
 * - GET /chain - Get blockchain state (mempool size, mining status)
 * - GET /block/:hash - Get block by hash
 * - GET /data/:txid - Get transaction data by ID
 * - POST /transaction - Submit new transaction
 * - POST /files - Upload file as transaction
 *
 * Thread safety:
 * - Uses atomic flags for state (f_running, f_stop_requested)
 * - Request queue is thread-safe with mutex + condition variable
 * - Blockweave operations are thread-safe (protected by blockweave's mutex)
 *
 * Example usage:
 *   CRestApiServer server(&blockweave, &config, "miner_address", 28443);
 *   server.Start();
 *   // ... server runs in background threads ...
 *   server.Stop();
 */
class CRestApiServer : public IRestApiServer {
private:
    CBlockweave* p_blockweave;                  ///< Pointer to blockweave instance
    const CConfig* p_config;                    ///< Configuration object
    std::string str_miner_address;              ///< Mining reward address
    int n_port;                                 ///< HTTP server port
    int n_server_socket;                        ///< Listening socket file descriptor

    std::atomic<bool> f_running;                ///< Server running state
    std::atomic<bool> f_stop_requested;         ///< Shutdown signal flag

    // Threads
    std::thread m_listener_thread;              ///< Thread accepting connections
    std::vector<std::thread> m_worker_threads;  ///< Worker threads processing requests

    // Request queue
    std::shared_ptr<CRequestQueue> p_request_queue;  ///< Shared request queue

    /**
     * @brief Listener thread function - accepts connections and enqueues requests
     *
     * Continuously accepts incoming connections, reads HTTP requests,
     * parses them, and adds to request queue for worker processing.
     */
    void ListenerThread();

    /**
     * @brief Worker thread function - processes requests from queue
     * @param n_worker_id Worker thread identifier (0-based)
     *
     * Continuously dequeues requests and processes them until shutdown.
     */
    void WorkerThread(int n_worker_id);

    /**
     * @brief Process HTTP request and send response
     * @param request Parsed HTTP request with client socket
     *
     * Routes request to appropriate handler based on method and path,
     * then sends HTTP response back to client.
     */
    void ProcessRequest(const CHttpRequest& request);

    /**
     * @brief Handle GET /chain endpoint
     * @return JSON response with blockchain state
     */
    std::string HandleGetChain();

    /**
     * @brief Handle GET /block/:hash endpoint
     * @param str_hash Block hash to retrieve
     * @return JSON response with block data
     */
    std::string HandleGetBlock(const std::string& str_hash);

    /**
     * @brief Handle GET /data/:txid endpoint
     * @param str_tx_id Transaction ID to retrieve
     * @return Transaction data as response body
     */
    std::string HandleGetData(const std::string& str_tx_id);

    /**
     * @brief Handle POST /transaction endpoint
     * @param str_body Request body containing transaction data
     * @return JSON response with transaction ID
     */
    std::string HandlePostTransaction(const std::string& str_body);

    /**
     * @brief Handle POST /files endpoint (multipart/form-data)
     * @param request HTTP request with file upload
     * @return JSON response with transaction ID
     */
    std::string HandlePostFiles(const CHttpRequest& request);

    /**
     * @brief Parse raw HTTP request into structured form
     * @param str_raw_request Raw HTTP request string from socket
     * @param n_client_socket Client socket for response
     * @return Parsed CHttpRequest structure
     */
    CHttpRequest ParseHttpRequest(const std::string& str_raw_request, int n_client_socket);

    /**
     * @brief Send HTTP response to client
     * @param n_client_socket Client socket to send response on
     * @param n_status_code HTTP status code (200, 404, 500, etc.)
     * @param str_content_type Content-Type header value
     * @param str_body Response body content
     */
    void SendHttpResponse(int n_client_socket, int n_status_code,
                          const std::string& str_content_type,
                          const std::string& str_body);

public:
    /**
     * @brief Construct REST API server
     * @param p_weave Pointer to blockweave instance
     * @param p_cfg Pointer to configuration object
     * @param str_miner_addr Mining reward address
     * @param n_port_num HTTP server port (default: 28443)
     */
    CRestApiServer(CBlockweave* p_weave, const CConfig* p_cfg, const std::string& str_miner_addr,
                   int n_port_num = 28443);

    /**
     * @brief Destructor - stops server and cleans up resources
     */
    virtual ~CRestApiServer() override;

    // IRestApiServer interface implementation

    /**
     * @brief Start REST API server
     * @return true if started successfully, false on error
     *
     * Creates listening socket and starts listener + worker threads.
     */
    virtual bool Start() override;

    /**
     * @brief Stop REST API server
     *
     * Signals shutdown, closes listening socket, and joins all threads.
     */
    virtual void Stop() override;

    /**
     * @brief Check if server is running
     * @return true if running, false otherwise
     */
    virtual bool IsRunning() const override;

    /**
     * @brief Handle GET request (interface method)
     * @param str_endpoint Endpoint path
     * @param request HTTP request structure
     * @return Response body
     */
    virtual std::string HandleGET(const std::string& str_endpoint, const CHttpRequest& request) override;

    /**
     * @brief Handle POST request (interface method)
     * @param str_endpoint Endpoint path
     * @param request HTTP request structure
     * @return Response body
     */
    virtual std::string HandlePOST(const std::string& str_endpoint, const CHttpRequest& request) override;
};

#endif
