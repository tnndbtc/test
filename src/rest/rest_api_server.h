// ============= rest_api_server.h =============
/**
 * @file rest_api_server.h
 * @brief Multi-threaded HTTP REST API server for blockweave
 *
 * Implements HTTP REST API with listener/worker thread architecture.
 * Listener thread accepts connections and enqueues requests, worker
 * threads process requests from queue concurrently.
 */

#ifndef REST_API_SERVER_H
#define REST_API_SERVER_H

#include "i_rest_api.h"
#include "request_queue.h"
#include "blockcore/block_weave.h"
#include "utils/settings.h"
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

// Forward declarations
class CConfig;
class CPeerManager;

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
 *
 * Public GET endpoints (no authentication required):
 * - GET /chain - Get blockchain state (mempool size, mining status)
 * - GET /transaction?hash=<hash> - Get transaction by hash
 * - GET /block?hash=<hash> - Get block by hash
 *
 * Authenticated RPC endpoints (require cookie-based HTTP Basic Auth):
 * - POST /rpc/transaction - Submit new transaction
 * - POST /rpc/addpeer - Add outbound peer connection
 * - POST /rpc/getpeer - Get list of connected peers
 * - POST /rpc/ping - Send PING to all connected peers immediately
 * - POST /rpc/minetrigger - Mine one block immediately (localnet only)
 * - POST /rpc/setmocktime - Set mock time for testing (localnet only)
 * - POST /rpc/triggerrotation - Trigger peer rotation check (localnet only)
 * - POST /rpc/disconnectpeer - Disconnect a specific peer (localnet only)
 *
 * Thread safety:
 * - Uses atomic flags for state (f_running, f_stop_requested)
 * - Request queue is thread-safe with mutex + condition variable
 * - Blockweave operations are thread-safe (protected by blockweave's mutex)
 *
 * Example usage:
 *   CRestApiServer server(&blockweave, &peer_manager, &config, "miner_address", 28443);
 *   server.Start();
 *   // ... server runs in background threads ...
 *   server.Stop();
 */
class CRestApiServer : public IRestApiServer {
private:
    CBlockweave* p_blockweave;                  ///< Pointer to blockweave instance
    CPeerManager* p_peer_manager;               ///< Pointer to peer manager instance
    const CConfig* p_config;                    ///< Configuration object
    std::string str_miner_address;              ///< Mining reward address
    int n_port;                                 ///< HTTP server port

    // Boost.Beast components
    boost::asio::io_context m_io_context;       ///< Asio I/O context for socket operations
    std::unique_ptr<boost::asio::ip::tcp::acceptor> m_acceptor;  ///< TCP acceptor for incoming connections

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
     * @return Tuple of (HTTP status code, JSON response with blockchain state)
     */
    std::tuple<int, std::string> HandleGetChain();

    /**
     * @brief Handle GET /transaction?hash=<hash> endpoint
     * @param str_endpoint Full endpoint string including query parameters
     * @return Tuple of (HTTP status code, JSON response with transaction data)
     */
    std::tuple<int, std::string> HandleGetTransaction(const std::string& str_endpoint);

    /**
     * @brief Handle GET /block?hash=<hash> endpoint
     * @param str_endpoint Full endpoint string including query parameters
     * @return Tuple of (HTTP status code, JSON response with block information)
     */
    std::tuple<int, std::string> HandleGetBlock(const std::string& str_endpoint);

    /**
     * @brief Handle POST /transaction endpoint
     * @param str_body Request body containing transaction data
     * @return Tuple of (HTTP status code, JSON response with transaction ID)
     */
    std::tuple<int, std::string> HandlePostTransaction(const std::string& str_body);

    /**
     * @brief Handle POST /rpc/addpeer endpoint
     * @param str_body Request body containing peer address and port
     * @return Tuple of (HTTP status code, JSON response with result)
     */
    std::tuple<int, std::string> HandleRpcAddPeer(const std::string& str_body);

    /**
     * @brief Handle POST /rpc/getpeer endpoint
     * @return Tuple of (HTTP status code, JSON response with peer list)
     */
    std::tuple<int, std::string> HandleRpcGetPeer();

    /**
     * @brief Handle POST /rpc/ping endpoint - send PING to all connected peers immediately
     * @return Tuple of (HTTP status code, JSON response with result)
     */
    std::tuple<int, std::string> HandleRpcPing();

    /**
     * @brief Handle POST /rpc/minetrigger endpoint - mine one block immediately (localnet only)
     * @return Tuple of (HTTP status code, JSON response with result)
     */
    std::tuple<int, std::string> HandleRpcMineTrigger();

    /**
     * @brief Handle POST /rpc/setmocktime endpoint - set mock time for testing (localnet only)
     * @param str_body Request body containing timestamp in seconds
     * @return Tuple of (HTTP status code, JSON response with result)
     */
    std::tuple<int, std::string> HandleRpcSetMockTime(const std::string& str_body);

    /**
     * @brief Handle POST /rpc/triggerrotation endpoint - trigger peer rotation check (localnet only)
     * @return Tuple of (HTTP status code, JSON response with result)
     */
    std::tuple<int, std::string> HandleRpcTriggerRotation();

    /**
     * @brief Handle POST /rpc/disconnectpeer endpoint - disconnect a specific peer (localnet only)
     * @param str_body Request body containing peer address and port
     * @return Tuple of (HTTP status code, JSON response with result)
     */
    std::tuple<int, std::string> HandleRpcDisconnectPeer(const std::string& str_body);

    /**
     * @brief Load RPC authentication credentials from .cookie file
     * @param str_cookie_path Path to .cookie file
     * @return true if loaded successfully, false otherwise
     */
    bool LoadCookieFile(const std::string& str_cookie_path);

    /**
     * @brief Validate HTTP Basic Auth credentials
     * @param str_auth_header Authorization header value
     * @return true if valid, false otherwise
     */
    bool ValidateBasicAuth(const std::string& str_auth_header) const;

    /**
     * @brief Check if endpoint requires authentication
     * @param str_path Request path
     * @return true if endpoint is /rpc/..., false otherwise
     */
    bool RequiresAuth(const std::string& str_path) const;

    /**
     * @brief Send HTTP response to client using Boost.Beast
     * @param socket Beast TCP socket to send response on
     * @param n_status_code HTTP status code (200, 404, 500, etc.)
     * @param str_content_type Content-Type header value
     * @param str_body Response body content
     */
    void SendHttpResponse(boost::asio::ip::tcp::socket& socket,
                          int n_status_code,
                          const std::string& str_content_type,
                          const std::string& str_body);

    // RPC authentication
    std::string m_str_cookie_username;  ///< RPC auth username (always "__cookie__")
    std::string m_str_cookie_password;  ///< RPC auth password from .cookie file
    bool m_f_auth_enabled;              ///< Whether RPC auth is enabled

public:
    /**
     * @brief Construct REST API server
     * @param p_weave Pointer to blockweave instance
     * @param p_peer_mgr Pointer to peer manager instance
     * @param p_cfg Pointer to configuration object
     * @param str_miner_addr Mining reward address
     * @param n_port_num HTTP server port (default: 28443)
     */
    CRestApiServer(CBlockweave* p_weave, CPeerManager* p_peer_mgr, const CConfig* p_cfg,
                   const std::string& str_miner_addr, int n_port_num = 28443);

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
     * @return Tuple of (HTTP status code, response body)
     */
    virtual std::tuple<int, std::string> HandleGET(const std::string& str_endpoint) override;

    /**
     * @brief Handle POST request (interface method)
     * @param str_endpoint Endpoint path
     * @param request HTTP request structure
     * @return Tuple of (HTTP status code, response body)
     */
    virtual std::tuple<int, std::string> HandlePOST(const std::string& str_endpoint, const CHttpRequest& request) override;
};

#endif
