#pragma once
#include "eviction_policy.hpp"
#include <map>
#include <set>
#include <unordered_map>
#include <chrono>
#include <stdexcept>

class HybridPolicy : public EvictionPolicy {
public:
    explicit HybridPolicy(double decayFactor = 0.5, int decayWindowMs = 2000)
        : decayFactor_(decayFactor), decayWindowMs_(decayWindowMs) {}

    void onInsert(const std::string& key) override {
        double score = 1.0;
        scoreIndex_[score].insert(key);
        info_[key] = {score, nowMs()};
    }

    void onAccess(const std::string& key) override {
        auto it = info_.find(key);
        if (it == info_.end()) return;
        double decayed = decayedScore(it->second.first, it->second.second);
        eraseFromIndex(it->second.first, key);
        double newScore = decayed + 1.0;
        scoreIndex_[newScore].insert(key);
        it->second = {newScore, nowMs()};
    }

    void onRemove(const std::string& key) override {
        auto it = info_.find(key);
        if (it == info_.end()) return;
        eraseFromIndex(it->second.first, key);
        info_.erase(it);
    }

    std::string evictCandidate() const override {
        if (scoreIndex_.empty()) throw std::runtime_error("Hybrid: evict on empty policy");
        auto& lowestBucket = scoreIndex_.begin()->second;
        if (lowestBucket.empty()) throw std::runtime_error("Hybrid: inconsistent score index");
        return *lowestBucket.begin();
    }

    std::string name() const override { return "Hybrid-WindowedLFU"; }

private:
    long long nowMs() const {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }

    double decayedScore(double score, long long lastTouchedMs) const {
        long long elapsed = nowMs() - lastTouchedMs;
        if (elapsed <= 0 || decayWindowMs_ <= 0) return score;
        long long windows = elapsed / decayWindowMs_;
        if (windows <= 0) return score;
        double factor = 1.0;
        for (long long i = 0; i < windows && i < 64; ++i) factor *= decayFactor_;
        return score * factor;
    }

    void eraseFromIndex(double score, const std::string& key) {
        auto it = scoreIndex_.find(score);
        if (it == scoreIndex_.end()) return;
        it->second.erase(key);
        if (it->second.empty()) scoreIndex_.erase(it);
    }

    double decayFactor_;
    int decayWindowMs_;
    std::map<double, std::set<std::string>> scoreIndex_;
    std::unordered_map<std::string, std::pair<double, long long>> info_;
};
