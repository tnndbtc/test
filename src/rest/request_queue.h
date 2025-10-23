// ============= request_queue.h =============
/**
 * @file request_queue.h
 * @brief Thread-safe request queue for REST API server
 *
 * Provides a producer-consumer queue for HTTP request handling.
 * Used by REST API server to coordinate between listener and worker threads.
 */

#ifndef REQUEST_QUEUE_H
#define REQUEST_QUEUE_H

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

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

#endif
