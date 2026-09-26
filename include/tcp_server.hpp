#pragma once
#include "sharded_cache.hpp"
#include "thread_pool.hpp"
#include <string>
#include <atomic>

class TcpServer {
public:
    TcpServer(int port, ShardedCache& cache, size_t numWorkerThreads);
    ~TcpServer();

    void run();
    void stop();

private:
    void handleConnection(int clientFd);
    std::string processLine(const std::string& line);

    int port_;
    int listenFd_ = -1;
    ShardedCache& cache_;
    ThreadPool pool_;
    std::atomic<bool> running_{false};
};
