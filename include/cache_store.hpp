#pragma once
#include "eviction_policy.hpp"
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <chrono>
#include <optional>
#include <algorithm>

struct ValueEntry {
    std::string value;
    bool hasTTL = false;
    std::chrono::steady_clock::time_point expiresAt;
};

struct CacheStats {
    long long hits = 0;
    long long misses = 0;
    long long evictions = 0;
    long long expirations = 0;
};

class CacheStore {
public:
    CacheStore(size_t capacity, std::unique_ptr<EvictionPolicy> policy)
        : capacity_(std::max<size_t>(1, capacity)), policy_(std::move(policy)) {}

    void set(const std::string& key, const std::string& value, std::optional<int> ttlSeconds) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = data_.find(key);
        if (it != data_.end()) {
            it->second.value = value;
            applyTTL(it->second, ttlSeconds);
            policy_->onAccess(key);
            return;
        }
        if (data_.size() >= capacity_) {
            evictOneLocked();
        }
        ValueEntry entry;
        entry.value = value;
        applyTTL(entry, ttlSeconds);
        data_.emplace(key, std::move(entry));
        policy_->onInsert(key);
    }

    std::optional<std::string> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) {
            stats_.misses++;
            return std::nullopt;
        }
        if (isExpired(it->second)) {
            data_.erase(it);
            policy_->onRemove(key);
            stats_.expirations++;
            stats_.misses++;
            return std::nullopt;
        }
        policy_->onAccess(key);
        stats_.hits++;
        return it->second.value;
    }

    bool del(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) return false;
        data_.erase(it);
        policy_->onRemove(key);
        return true;
    }

    bool expire(const std::string& key, int ttlSeconds) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) return false;
        applyTTL(it->second, ttlSeconds);
        return true;
    }

    CacheStats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.size();
    }

    std::string policyName() const { return policy_->name(); }

private:
    void applyTTL(ValueEntry& entry, std::optional<int> ttlSeconds) {
        if (ttlSeconds.has_value()) {
            entry.hasTTL = true;
            entry.expiresAt = std::chrono::steady_clock::now() + std::chrono::seconds(*ttlSeconds);
        } else {
            entry.hasTTL = false;
        }
    }

    bool isExpired(const ValueEntry& entry) const {
        return entry.hasTTL && std::chrono::steady_clock::now() >= entry.expiresAt;
    }

    void evictOneLocked() {
        std::string victim = policy_->evictCandidate();
        data_.erase(victim);
        policy_->onRemove(victim);
        stats_.evictions++;
    }

    size_t capacity_;
    std::unique_ptr<EvictionPolicy> policy_;
    std::unordered_map<std::string, ValueEntry> data_;
    mutable std::mutex mutex_;
    CacheStats stats_;
};
