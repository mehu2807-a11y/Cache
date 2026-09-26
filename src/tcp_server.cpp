#include "tcp_server.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sstream>
#include <vector>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <optional>

TcpServer::TcpServer(int port, ShardedCache& cache, size_t numWorkerThreads)
    : port_(port), cache_(cache), pool_(numWorkerThreads) {}

TcpServer::~TcpServer() {
    stop();
}

void TcpServer::stop() {
    bool wasRunning = running_.exchange(false);
    if (wasRunning && listenFd_ >= 0) {
        shutdown(listenFd_, SHUT_RDWR);
        close(listenFd_);
        listenFd_ = -1;
    }
}

void TcpServer::run() {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) { std::cerr << "socket() failed\n"; return; }

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed on port " << port_ << "\n";
        return;
    }
    if (listen(listenFd_, 128) < 0) {
        std::cerr << "listen() failed\n";
        return;
    }
    running_ = true;

    while (running_.load()) {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int clientFd = accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &len);
        if (clientFd < 0) {
            if (!running_.load()) break;
            continue;
        }
        pool_.enqueue([this, clientFd] { handleConnection(clientFd); });
    }
}

void TcpServer::handleConnection(int clientFd) {
    std::string buffer;
    char chunk[4096];
    while (true) {
        ssize_t n = recv(clientFd, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        buffer.append(chunk, static_cast<size_t>(n));

        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            std::string response = processLine(line) + "\n";
            if (send(clientFd, response.c_str(), response.size(), 0) < 0) {
                close(clientFd);
                return;
            }
        }
    }
    close(clientFd);
}

namespace {
std::vector<std::string> splitTokens(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);
    return tokens;
}

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}
}

std::string TcpServer::processLine(const std::string& line) {
    auto tokens = splitTokens(line);
    if (tokens.empty()) return "ERROR empty command";
    std::string cmd = toUpper(tokens[0]);

    if (cmd == "SET") {
        if (tokens.size() < 3) return "ERROR usage: SET key value [ttl_seconds]";
        std::optional<int> ttl;
        if (tokens.size() >= 4) {
            try { ttl = std::stoi(tokens[3]); }
            catch (...) { return "ERROR invalid ttl"; }
        }
        cache_.set(tokens[1], tokens[2], ttl);
        return "OK";
    }
    if (cmd == "GET") {
        if (tokens.size() != 2) return "ERROR usage: GET key";
        auto val = cache_.get(tokens[1]);
        if (!val) return "NULL";
        return "VALUE " + *val;
    }
    if (cmd == "DEL") {
        if (tokens.size() != 2) return "ERROR usage: DEL key";
        return cache_.del(tokens[1]) ? "OK" : "NOTFOUND";
    }
    if (cmd == "EXPIRE") {
        if (tokens.size() != 3) return "ERROR usage: EXPIRE key seconds";
        int sec;
        try { sec = std::stoi(tokens[2]); }
        catch (...) { return "ERROR invalid seconds"; }
        return cache_.expire(tokens[1], sec) ? "OK" : "NOTFOUND";
    }
    return "ERROR unknown command: " + cmd;
}
