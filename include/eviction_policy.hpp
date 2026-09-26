#pragma once
#include <string>

class EvictionPolicy {
public:
    virtual ~EvictionPolicy() = default;
    virtual void onInsert(const std::string& key) = 0;
    virtual void onAccess(const std::string& key) = 0;
    virtual void onRemove(const std::string& key) = 0;
    virtual std::string evictCandidate() const = 0;
    virtual std::string name() const = 0;
};
