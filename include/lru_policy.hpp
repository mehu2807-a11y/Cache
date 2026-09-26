#pragma once
#include "eviction_policy.hpp"
#include <list>
#include <unordered_map>
#include <stdexcept>

class LRUPolicy : public EvictionPolicy {
public:
    void onInsert(const std::string& key) override {
        order_.push_front(key);
        pos_[key] = order_.begin();
    }

    void onAccess(const std::string& key) override {
        auto it = pos_.find(key);
        if (it == pos_.end()) return;
        order_.erase(it->second);
        order_.push_front(key);
        it->second = order_.begin();
    }

    void onRemove(const std::string& key) override {
        auto it = pos_.find(key);
        if (it == pos_.end()) return;
        order_.erase(it->second);
        pos_.erase(it);
    }

    std::string evictCandidate() const override {
        if (order_.empty()) throw std::runtime_error("LRU: evict on empty policy");
        return order_.back();
    }

    std::string name() const override { return "LRU"; }

private:
    std::list<std::string> order_;
    std::unordered_map<std::string, std::list<std::string>::iterator> pos_;
};
