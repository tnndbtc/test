#ifndef REST_SERVER_H
#define REST_SERVER_H

#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <functional>

class RestServer {
public:
    RestServer(int port = 8080, int num_threads = 5);
    ~RestServer();

    void start();
    void stop();
    bool is_running() const;

private:
    void worker_thread(int thread_id);
    void handle_request(int client_socket);

    int port_;
    int num_threads_;
    std::atomic<bool> running_;
    std::vector<std::thread> threads_;
    int server_socket_;
};

#endif // REST_SERVER_H
