#pragma once

#include <string>
#include <unordered_map>
#include <mutex>

class KVStore {
public:
    KVStore() = default;

    void set(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& outValue);

private:
    std::unordered_map<std::string, std::string> store_;
    std::mutex mutex_;
};
