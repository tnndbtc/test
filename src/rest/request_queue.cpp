// ============= request_queue.cpp =============
#include "request_queue.h"
#include <chrono>

// ============= CRequestQueue Implementation =============

/**
 * @brief Constructor - initializes empty queue with shutdown flag false
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
 * to wake up and check shutdown condition.
 */
void CRequestQueue::Shutdown() {
    f_shutdown = true;
    cv_queue.notify_all();
}

/**
 * @brief Reset shutdown flag for restarting the queue
 *
 * Clears the shutdown flag to allow the queue to be reused
 * after Stop() has been called. This enables multiple start/stop
 * cycles without recreating the queue.
 */
void CRequestQueue::Reset() {
    std::lock_guard<std::mutex> lock(cs_queue);
    f_shutdown = false;
    // Clear any remaining requests from previous run
    while (!m_queue.empty()) {
        m_queue.pop();
    }
}

/**
 * @brief Get current queue size (thread-safe)
 * @return Number of requests in queue
 */
size_t CRequestQueue::Size() const {
    std::lock_guard<std::mutex> lock(cs_queue);
    return m_queue.size();
}
