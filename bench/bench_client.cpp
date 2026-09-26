#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <algorithm>
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

struct ThreadResult {
    std::vector<double> latenciesUs;
    long long hits = 0;
    long long misses = 0;
};

static int connectTo(const std::string& host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static std::string sendAndRecv(int fd, const std::string& line) {
    std::string msg = line + "\n";
    if (send(fd, msg.c_str(), msg.size(), 0) < 0) return "";
    char buf[4096];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return "";
    buf[n] = '\0';
    return std::string(buf);
}

static void worker(const std::string& host, int port,
                    const std::vector<std::string>& lines,
                    ThreadResult& result) {
    int fd = connectTo(host, port);
    if (fd < 0) {
        std::cerr << "bench_client: connect failed\n";
        return;
    }
    result.latenciesUs.reserve(lines.size());
    for (const auto& line : lines) {
        auto t0 = std::chrono::steady_clock::now();
        std::string resp = sendAndRecv(fd, line);
        auto t1 = std::chrono::steady_clock::now();
        result.latenciesUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());

        if (line.rfind("GET", 0) == 0) {
            if (resp.rfind("VALUE", 0) == 0) result.hits++;
            else if (resp.rfind("NULL", 0) == 0) result.misses++;
        }
    }
    close(fd);
}

static double percentile(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    size_t idx = static_cast<size_t>(p * static_cast<double>(sorted.size() - 1));
    return sorted[idx];
}

int main(int argc, char** argv) {
    if (argc < 8) {
        std::cerr << "Usage: bench_client host port trace_file concurrency policy_label pattern_label cache_size_label\n";
        return 1;
    }
    std::string host = argv[1];
    int port = std::stoi(argv[2]);
    std::string traceFile = argv[3];
    int concurrency = std::max(1, std::stoi(argv[4]));
    std::string policyLabel = argv[5];
    std::string patternLabel = argv[6];
    std::string cacheSizeLabel = argv[7];

    std::ifstream in(traceFile);
    if (!in) {
        std::cerr << "bench_client: cannot open trace file " << traceFile << "\n";
        return 1;
    }
    std::vector<std::string> allLines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) allLines.push_back(line);
    }

    std::vector<std::vector<std::string>> chunks(concurrency);
    for (size_t i = 0; i < allLines.size(); ++i) {
        chunks[i % static_cast<size_t>(concurrency)].push_back(allLines[i]);
    }

    std::vector<ThreadResult> results(concurrency);
    std::vector<std::thread> threads;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < concurrency; ++i) {
        threads.emplace_back(worker, host, port, std::cref(chunks[i]), std::ref(results[i]));
    }
    for (auto& t : threads) t.join();
    auto end = std::chrono::steady_clock::now();

    double totalSeconds = std::chrono::duration<double>(end - start).count();
    std::vector<double> allLatencies;
    long long totalHits = 0, totalMisses = 0;
    long long totalRequests = 0;
    for (auto& r : results) {
        allLatencies.insert(allLatencies.end(), r.latenciesUs.begin(), r.latenciesUs.end());
        totalHits += r.hits;
        totalMisses += r.misses;
        totalRequests += static_cast<long long>(r.latenciesUs.size());
    }
    std::sort(allLatencies.begin(), allLatencies.end());

    double p50 = percentile(allLatencies, 0.50);
    double p95 = percentile(allLatencies, 0.95);
    double p99 = percentile(allLatencies, 0.99);
    double throughput = totalSeconds > 0 ? static_cast<double>(totalRequests) / totalSeconds : 0.0;
    double hitRate = (totalHits + totalMisses) > 0
        ? static_cast<double>(totalHits) / static_cast<double>(totalHits + totalMisses)
        : 0.0;

    std::cout << policyLabel << "," << patternLabel << "," << cacheSizeLabel << ","
              << concurrency << "," << throughput << "," << p50 << "," << p95 << ","
              << p99 << "," << hitRate << "\n";
    return 0;
}
