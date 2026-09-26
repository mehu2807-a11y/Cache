#pragma once
#include "cache_store.hpp"
#include "lru_policy.hpp"
#include "lfu_policy.hpp"
#include "hybrid_policy.hpp"
#include <vector>
#include <functional>
#include <stdexcept>
#include <algorithm>

enum class PolicyType { LRU, LFU, HYBRID };

inline std::unique_ptr<EvictionPolicy> makePolicy(PolicyType t) {
    switch (t) {
        case PolicyType::LRU:    return std::make_unique<LRUPolicy>();
        case PolicyType::LFU:    return std::make_unique<LFUPolicy>();
        case PolicyType::HYBRID: return std::make_unique<HybridPolicy>();
    }
    throw std::invalid_argument("unknown policy type");
}

inline PolicyType parsePolicy(const std::string& s) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "lru") return PolicyType::LRU;
    if (lower == "lfu") return PolicyType::LFU;
    if (lower == "hybrid") return PolicyType::HYBRID;
    throw std::invalid_argument("unknown policy name: " + s);
}

class ShardedCache {
public:
    ShardedCache(size_t totalCapacity, size_t numShards, PolicyType policyType)
        : numShards_(std::max<size_t>(1, numShards)) {
        size_t perShard = std::max<size_t>(1, totalCapacity / numShards_);
        shards_.reserve(numShards_);
        for (size_t i = 0; i < numShards_; ++i) {
            shards_.push_back(std::make_unique<CacheStore>(perShard, makePolicy(policyType)));
        }
    }

    void set(const std::string& key, const std::string& value, std::optional<int> ttl) {
        shardFor(key).set(key, value, ttl);
    }
    std::optional<std::string> get(const std::string& key) { return shardFor(key).get(key); }
    bool del(const std::string& key) { return shardFor(key).del(key); }
    bool expire(const std::string& key, int ttlSeconds) { return shardFor(key).expire(key, ttlSeconds); }

    CacheStats aggregateStats() const {
        CacheStats total;
        for (auto& s : shards_) {
            auto st = s->stats();
            total.hits += st.hits;
            total.misses += st.misses;
            total.evictions += st.evictions;
            total.expirations += st.expirations;
        }
        return total;
    }

    size_t totalSize() const {
        size_t total = 0;
        for (auto& s : shards_) total += s->size();
        return total;
    }

private:
    CacheStore& shardFor(const std::string& key) {
        size_t h = std::hash<std::string>{}(key) % numShards_;
        return *shards_[h];
    }

    size_t numShards_;
    std::vector<std::unique_ptr<CacheStore>> shards_;
};
