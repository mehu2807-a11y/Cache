#include "sharded_cache.hpp"
#include "tcp_server.hpp"
#include <iostream>
#include <csignal>
#include <cstdlib>

static TcpServer* g_server = nullptr;

void handleSignal(int) {
    if (g_server) g_server->stop();
}

int main(int argc, char** argv) {
    int port = 6380;
    size_t capacity = 10000;
    size_t shards = 4;
    size_t workerThreads = 64;
    std::string policyStr = "lru";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](const char* flag) -> std::string {
            if (i + 1 < argc) return argv[++i];
            std::cerr << "missing value for " << flag << "\n";
            std::exit(1);
        };
        if (arg == "--port") port = std::stoi(next("--port"));
        else if (arg == "--capacity") capacity = std::stoul(next("--capacity"));
        else if (arg == "--shards") shards = std::stoul(next("--shards"));
        else if (arg == "--threads") workerThreads = std::stoul(next("--threads"));
        else if (arg == "--policy") policyStr = next("--policy");
        else {
            std::cerr << "unknown argument: " << arg << "\n";
            return 1;
        }
    }

    PolicyType policyType;
    try {
        policyType = parsePolicy(policyStr);
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    ShardedCache cache(capacity, shards, policyType);
    TcpServer server(port, cache, workerThreads);
    g_server = &server;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    server.run();
    return 0;
}
