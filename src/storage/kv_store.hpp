// Key-Value storage implementation for Mini-Redis
// Provides thread-safe in-memory storage with expiration, LRU eviction, and persistence

#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <ctime>
#include <list>

class KVStore {
public:
    KVStore() = default;

    void set(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& outValue);
    bool del(const std::string& key);
    bool exists(const std::string& key);
    std::vector<std::string> keys();
    bool expire(const std::string& key, int seconds);
    int ttl(const std::string& key);
    size_t size() const;
    void save_to_file(const std::string& filename) const;
    void load_from_file(const std::string& filename);
    bool save_to_rdb(const std::string& filename) const;
    bool load_from_rdb(const std::string& filename);

private:
    // Check if key is expired and remove it if so (must be called with lock held)
    void check_and_remove_expired(const std::string& key);
    // Update LRU access order (must be called with lock held)
    void update_lru(const std::string& key);
    // Evict oldest key if store exceeds threshold (must be called with lock held)
    void evict_if_needed();

    std::unordered_map<std::string, std::string> store_;
    std::unordered_map<std::string, time_t> expirations_; // Key -> expiration timestamp
    std::list<std::string> lru_order_; // Most recent at front, oldest at back
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_map_; // Key -> position in lru_order_
    mutable std::mutex mutex_;
    static const size_t MAX_KEYS = 10000; // LRU eviction threshold
};
