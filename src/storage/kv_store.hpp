#pragma once

#include <string>
#include <unordered_map>
#include <mutex>

namespace mini_redis {

class KvStore {
public:
    // Stores key → value
    void set(const std::string& key, const std::string& value);

    // Returns value; empty string if key not found
    std::string get(const std::string& key);

    // Returns true if key existed and was removed
    bool del(const std::string& key);

private:
    std::unordered_map<std::string, std::string> data_;
    std::mutex mutex_;
};

} // namespace mini_redis
