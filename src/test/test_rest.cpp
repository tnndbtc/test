// ============= test_rest.cpp =============
#include "unit_test.h"
#include "rest/rest_api.h"
#include "blockcore/blockweave.h"
#include "cli/config.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

using namespace UnitTest;

/**
 * @brief Test CHttpRequest structure initialization
 */
TEST(HttpRequest_Initialization) {
    CHttpRequest request;
    request.str_method = "GET";
    request.str_path = "/chain";
    request.str_body = "";
    request.str_content_type = "application/json";
    request.n_client_socket = 42;

    ASSERT_EQUAL(request.str_method, std::string("GET"), "Method should be GET");
    ASSERT_EQUAL(request.str_path, std::string("/chain"), "Path should be /chain");
    ASSERT_TRUE(request.str_body.empty(), "Body should be empty");
    ASSERT_EQUAL(request.str_content_type, std::string("application/json"), "Content type should be set");
    ASSERT_EQUAL(request.n_client_socket, 42, "Socket should be 42");
}

/**
 * @brief Test CRequestQueue default constructor
 */
TEST(RequestQueue_Constructor) {
    CRequestQueue queue;

    ASSERT_EQUAL(queue.Size(), (size_t)0, "Queue should be empty initially");
}

/**
 * @brief Test CRequestQueue enqueue operation
 */
TEST(RequestQueue_Enqueue) {
    CRequestQueue queue;

    CHttpRequest request;
    request.str_method = "GET";
    request.str_path = "/chain";
    request.n_client_socket = 1;

    queue.Enqueue(request);

    ASSERT_EQUAL(queue.Size(), (size_t)1, "Queue should have 1 item after enqueue");
}

/**
 * @brief Test CRequestQueue enqueue and dequeue
 */
TEST(RequestQueue_EnqueueDequeue) {
    CRequestQueue queue;

    CHttpRequest request1;
    request1.str_method = "GET";
    request1.str_path = "/chain";
    request1.n_client_socket = 1;

    queue.Enqueue(request1);

    CHttpRequest request2;
    bool success = queue.Dequeue(request2, 1000);

    ASSERT_TRUE(success, "Dequeue should succeed");
    ASSERT_EQUAL(request2.str_method, std::string("GET"), "Method should match");
    ASSERT_EQUAL(request2.str_path, std::string("/chain"), "Path should match");
    ASSERT_EQUAL(request2.n_client_socket, 1, "Socket should match");
    ASSERT_EQUAL(queue.Size(), (size_t)0, "Queue should be empty after dequeue");
}

/**
 * @brief Test CRequestQueue dequeue timeout on empty queue
 */
TEST(RequestQueue_DequeueTimeout) {
    CRequestQueue queue;

    CHttpRequest request;
    auto start = std::chrono::steady_clock::now();
    bool success = queue.Dequeue(request, 100);  // 100ms timeout
    auto end = std::chrono::steady_clock::now();

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    ASSERT_FALSE(success, "Dequeue should fail on empty queue");
    ASSERT_TRUE(duration_ms >= 90, "Dequeue should wait at least ~100ms");
    ASSERT_TRUE(duration_ms < 200, "Dequeue should not wait much longer than timeout");
}

/**
 * @brief Test CRequestQueue FIFO ordering
 */
TEST(RequestQueue_FIFOOrdering) {
    CRequestQueue queue;

    // Enqueue 3 requests
    for (int i = 1; i <= 3; i++) {
        CHttpRequest request;
        request.str_method = "GET";
        request.str_path = "/request" + std::to_string(i);
        request.n_client_socket = i;
        queue.Enqueue(request);
    }

    ASSERT_EQUAL(queue.Size(), (size_t)3, "Queue should have 3 items");

    // Dequeue and verify FIFO order
    for (int i = 1; i <= 3; i++) {
        CHttpRequest request;
        bool success = queue.Dequeue(request, 1000);
        ASSERT_TRUE(success, "Dequeue should succeed");
        ASSERT_EQUAL(request.str_path, std::string("/request" + std::to_string(i)),
                     "Should dequeue in FIFO order");
        ASSERT_EQUAL(request.n_client_socket, i, "Socket should match");
    }

    ASSERT_EQUAL(queue.Size(), (size_t)0, "Queue should be empty");
}

/**
 * @brief Test CRequestQueue shutdown mechanism
 */
TEST(RequestQueue_Shutdown) {
    CRequestQueue queue;

    // Start a thread waiting to dequeue
    std::atomic<bool> dequeue_returned{false};
    std::atomic<bool> dequeue_success{true};

    std::thread waiter([&queue, &dequeue_returned, &dequeue_success]() {
        CHttpRequest request;
        dequeue_success = queue.Dequeue(request, 10000);  // Long timeout
        dequeue_returned = true;
    });

    // Give the thread time to start waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Shutdown the queue
    queue.Shutdown();

    // Wait for thread to finish
    waiter.join();

    ASSERT_TRUE(dequeue_returned, "Dequeue should return after shutdown");
    ASSERT_FALSE(dequeue_success, "Dequeue should return false after shutdown");
}

/**
 * @brief Test CRequestQueue thread-safe enqueue from multiple threads
 */
TEST(RequestQueue_ThreadSafeEnqueue) {
    CRequestQueue queue;
    const int n_threads = 10;
    const int n_requests_per_thread = 100;

    std::vector<std::thread> threads;
    for (int i = 0; i < n_threads; i++) {
        threads.emplace_back([&queue, i, n_requests_per_thread]() {
            for (int j = 0; j < n_requests_per_thread; j++) {
                CHttpRequest request;
                request.str_method = "GET";
                request.str_path = "/thread" + std::to_string(i);
                request.n_client_socket = i * 1000 + j;
                queue.Enqueue(request);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQUAL(queue.Size(), (size_t)(n_threads * n_requests_per_thread),
                 "Queue should contain all enqueued requests");
}

/**
 * @brief Test CRequestQueue thread-safe concurrent enqueue and dequeue
 */
TEST(RequestQueue_ThreadSafeConcurrent) {
    CRequestQueue queue;
    std::atomic<int> enqueue_count{0};
    std::atomic<int> dequeue_count{0};
    std::atomic<bool> stop_flag{false};

    const int n_producers = 5;
    const int n_consumers = 5;

    // Producer threads
    std::vector<std::thread> producers;
    for (int i = 0; i < n_producers; i++) {
        producers.emplace_back([&queue, &enqueue_count, &stop_flag]() {
            while (!stop_flag) {
                CHttpRequest request;
                request.str_method = "POST";
                request.str_path = "/data";
                queue.Enqueue(request);
                enqueue_count++;
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }

    // Consumer threads
    std::vector<std::thread> consumers;
    for (int i = 0; i < n_consumers; i++) {
        consumers.emplace_back([&queue, &dequeue_count, &stop_flag]() {
            while (!stop_flag) {
                CHttpRequest request;
                if (queue.Dequeue(request, 100)) {
                    dequeue_count++;
                }
            }
        });
    }

    // Let threads run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    stop_flag = true;

    // Join all threads
    for (auto& t : producers) {
        t.join();
    }
    for (auto& t : consumers) {
        t.join();
    }

    // Drain remaining requests
    CHttpRequest request;
    while (queue.Dequeue(request, 10)) {
        dequeue_count++;
    }

    ASSERT_TRUE(enqueue_count > 100, "Should have enqueued many requests");
    ASSERT_EQUAL(enqueue_count.load(), dequeue_count.load(),
                 "All enqueued requests should be dequeued");
}

/**
 * @brief Test CRequestQueue multiple shutdown calls
 */
TEST(RequestQueue_MultipleShutdown) {
    CRequestQueue queue;

    // Shutdown multiple times should not crash
    queue.Shutdown();
    queue.Shutdown();
    queue.Shutdown();

    ASSERT_TRUE(true, "Multiple shutdown calls should not crash");
}

/**
 * @brief Test CRestApiServer constructor
 */
TEST(RestApiServer_Constructor) {
    CBlockweave blockweave;
    CConfig config;

    CRestApiServer server(&blockweave, &config, "test_miner_address", 28443);

    ASSERT_FALSE(server.IsRunning(), "Server should not be running initially");
}

/**
 * @brief Test CRestApiServer start and stop
 */
TEST(RestApiServer_StartStop) {
    CBlockweave blockweave;
    CConfig config;

    CRestApiServer server(&blockweave, &config, "test_miner_address", 38443);

    ASSERT_FALSE(server.IsRunning(), "Server should not be running initially");

    bool started = server.Start();
    ASSERT_TRUE(started, "Server should start successfully");
    ASSERT_TRUE(server.IsRunning(), "Server should be running after start");

    // Give server time to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    server.Stop();
    ASSERT_FALSE(server.IsRunning(), "Server should not be running after stop");
}

/**
 * @brief Test CRestApiServer multiple start/stop cycles
 */
TEST(RestApiServer_MultipleStartStop) {
    CBlockweave blockweave;
    CConfig config;

    CRestApiServer server(&blockweave, &config, "test_miner_address", 38444);

    for (int i = 0; i < 3; i++) {
        bool started = server.Start();
        ASSERT_TRUE(started, "Server should start on cycle " + std::to_string(i));

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        server.Stop();
        ASSERT_FALSE(server.IsRunning(), "Should stop on cycle " + std::to_string(i));
    }
}

/**
 * @brief Test CRestApiServer stop without start
 */
TEST(RestApiServer_StopWithoutStart) {
    CBlockweave blockweave;
    CConfig config;

    CRestApiServer server(&blockweave, &config, "test_miner_address", 38445);

    // Stop without starting should not crash
    server.Stop();

    ASSERT_FALSE(server.IsRunning(), "Server should not be running");
}

/**
 * @brief Test CRestApiServer destructor stops running server
 */
TEST(RestApiServer_DestructorStops) {
    CBlockweave blockweave;
    CConfig config;

    {
        CRestApiServer server(&blockweave, &config, "test_miner_address", 38446);
        server.Start();
        ASSERT_TRUE(server.IsRunning(), "Server should be running");

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Destructor will be called when server goes out of scope
    }

    // If we get here without crashing, the destructor worked correctly
    ASSERT_TRUE(true, "Destructor should stop server cleanly");
}

