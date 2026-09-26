#pragma once
#include "eviction_policy.hpp"
#include <list>
#include <map>
#include <unordered_map>
#include <stdexcept>

class LFUPolicy : public EvictionPolicy {
public:
    void onInsert(const std::string& key) override {
        buckets_[1].push_front(key);
        info_[key] = {1, buckets_[1].begin()};
        minFreq_ = 1;
    }

    void onAccess(const std::string& key) override {
        auto it = info_.find(key);
        if (it == info_.end()) return;
        int freq = it->second.first;
        removeFromBucket(freq, it->second.second);
        int newFreq = freq + 1;
        buckets_[newFreq].push_front(key);
        it->second = {newFreq, buckets_[newFreq].begin()};
    }

    void onRemove(const std::string& key) override {
        auto it = info_.find(key);
        if (it == info_.end()) return;
        removeFromBucket(it->second.first, it->second.second);
        info_.erase(it);
    }

    std::string evictCandidate() const override {
        auto it = buckets_.find(minFreq_);
        if (it == buckets_.end() || it->second.empty())
            throw std::runtime_error("LFU: evict on empty/inconsistent policy");
        return it->second.back();
    }

    std::string name() const override { return "LFU"; }

private:
    void removeFromBucket(int freq, std::list<std::string>::iterator iter) {
        auto bIt = buckets_.find(freq);
        if (bIt == buckets_.end()) return;
        bIt->second.erase(iter);
        if (bIt->second.empty()) {
            buckets_.erase(bIt);
            if (freq == minFreq_) {
                minFreq_ = buckets_.empty() ? 0 : buckets_.begin()->first;
            }
        }
    }

    std::map<int, std::list<std::string>> buckets_;
    std::unordered_map<std::string, std::pair<int, std::list<std::string>::iterator>> info_;
    int minFreq_ = 0;
};
