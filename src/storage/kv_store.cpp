// Key-Value storage implementation for Mini-Redis
// Provides thread-safe operations with expiration, LRU eviction, and file persistence

#include "kv_store.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdint>

void KVStore::check_and_remove_expired(const std::string& key) {
    auto exp_it = expirations_.find(key);
    if (exp_it != expirations_.end()) {
        time_t now = time(nullptr);
        if (now >= exp_it->second) {
            // Key has expired, remove it
            store_.erase(key);
            expirations_.erase(exp_it);
            // Remove from LRU tracking
            auto lru_it = lru_map_.find(key);
            if (lru_it != lru_map_.end()) {
                lru_order_.erase(lru_it->second);
                lru_map_.erase(lru_it);
            }
        }
    }
}

void KVStore::update_lru(const std::string& key) {
    // Remove from current position if exists
    auto lru_it = lru_map_.find(key);
    if (lru_it != lru_map_.end()) {
        lru_order_.erase(lru_it->second);
    }
    // Add to front (most recent)
    lru_order_.push_front(key);
    lru_map_[key] = lru_order_.begin();
}

void KVStore::evict_if_needed() {
    while (store_.size() > MAX_KEYS && !lru_order_.empty()) {
        // Remove oldest key (from back of list)
        std::string oldest_key = lru_order_.back();
        store_.erase(oldest_key);
        expirations_.erase(oldest_key);
        lru_map_.erase(oldest_key);
        lru_order_.pop_back();
    }
}

void KVStore::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    check_and_remove_expired(key);
    store_[key] = value;
    update_lru(key);
    evict_if_needed();
}

bool KVStore::get(const std::string& key, std::string& outValue) {
    std::lock_guard<std::mutex> lock(mutex_);
    check_and_remove_expired(key);
    auto it = store_.find(key);
    if (it == store_.end()) {
        return false;
    }
    outValue = it->second;
    update_lru(key);
    return true;
}

// DEL removes a key from the store if it exists
// Returns true if the key was removed, false if it didn't exist
bool KVStore::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    check_and_remove_expired(key);
    auto it = store_.find(key);
    if (it != store_.end()) {
        store_.erase(it);
        expirations_.erase(key);
        // Remove from LRU tracking
        auto lru_it = lru_map_.find(key);
        if (lru_it != lru_map_.end()) {
            lru_order_.erase(lru_it->second);
            lru_map_.erase(lru_it);
        }
        return true;
    }
    return false;
}

// EXISTS checks if a key exists in the store
// Returns true if the key exists, false otherwise
bool KVStore::exists(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    check_and_remove_expired(key);
    return store_.find(key) != store_.end();
}

// KEYS returns all keys currently in the store
// Returns a vector containing all key names
std::vector<std::string> KVStore::keys() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    result.reserve(store_.size());
    // Clean expired keys first
    std::vector<std::string> keys_to_check;
    for (const auto& pair : store_) {
        keys_to_check.push_back(pair.first);
    }
    for (const auto& key : keys_to_check) {
        check_and_remove_expired(key);
    }
    // Now collect remaining keys
    for (const auto& pair : store_) {
        result.push_back(pair.first);
    }
    return result;
}

// EXPIRE sets expiration time for a key in seconds
// Returns true if key exists and expiration was set, false if key doesn't exist
bool KVStore::expire(const std::string& key, int seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    check_and_remove_expired(key);
    if (store_.find(key) == store_.end()) {
        return false;
    }
    time_t now = time(nullptr);
    expirations_[key] = now + seconds;
    return true;
}

// TTL returns time-to-live in seconds for a key
// Returns -1 if key doesn't exist or has no expiration, otherwise returns remaining seconds
int KVStore::ttl(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    check_and_remove_expired(key);
    if (store_.find(key) == store_.end()) {
        return -2; // Key doesn't exist
    }
    auto exp_it = expirations_.find(key);
    if (exp_it == expirations_.end()) {
        return -1; // No expiration set
    }
    time_t now = time(nullptr);
    int remaining = static_cast<int>(exp_it->second - now);
    return remaining > 0 ? remaining : -2; // -2 if already expired (shouldn't happen after check)
}

// SIZE returns the number of keys in the store
size_t KVStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return store_.size();
}

// SAVE writes the store to a file in simple format: key=value (one per line)
void KVStore::save_to_file(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream file(filename);
    if (!file.is_open()) {
        return;
    }
    for (const auto& pair : store_) {
        // Escape newlines and = in key/value
        std::string key = pair.first;
        std::string value = pair.second;
        std::replace(key.begin(), key.end(), '\n', ' ');
        std::replace(key.begin(), key.end(), '=', ' ');
        std::replace(value.begin(), value.end(), '\n', ' ');
        std::replace(value.begin(), value.end(), '=', ' ');
        file << key << "=" << value << "\n";
    }
}

// LOAD reads the store from a file
void KVStore::load_from_file(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream file(filename);
    if (!file.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        size_t pos = line.find('=');
        if (pos != std::string::npos && pos > 0 && pos < line.length() - 1) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            store_[key] = value;
            update_lru(key);
        }
    }
    evict_if_needed();
}

// SAVE_TO_RDB writes the store to a file in binary RDB format
// Format: [num_keys: uint32] then for each key:
//   [key_length: uint32][key_bytes][value_length: uint32][value_bytes][expiry_timestamp: int64]
// Returns true on success, false on error
bool KVStore::save_to_rdb(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Clean expired keys first to avoid saving them
    std::vector<std::string> keys_to_check;
    for (const auto& pair : store_) {
        keys_to_check.push_back(pair.first);
    }
    for (const auto& key : keys_to_check) {
        const_cast<KVStore*>(this)->check_and_remove_expired(key);
    }
    
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Write number of keys
    uint32_t num_keys = static_cast<uint32_t>(store_.size());
    file.write(reinterpret_cast<const char*>(&num_keys), sizeof(uint32_t));
    
    if (!file.good()) {
        return false;
    }
    
    // Write each key-value pair
    for (const auto& pair : store_) {
        const std::string& key = pair.first;
        const std::string& value = pair.second;
        
        // Write key length and key bytes
        uint32_t key_len = static_cast<uint32_t>(key.size());
        file.write(reinterpret_cast<const char*>(&key_len), sizeof(uint32_t));
        file.write(key.c_str(), key_len);
        
        // Write value length and value bytes
        uint32_t value_len = static_cast<uint32_t>(value.size());
        file.write(reinterpret_cast<const char*>(&value_len), sizeof(uint32_t));
        file.write(value.c_str(), value_len);
        
        // Write expiration timestamp (0 if no expiration)
        int64_t expiry = 0;
        auto exp_it = expirations_.find(key);
        if (exp_it != expirations_.end()) {
            expiry = static_cast<int64_t>(exp_it->second);
        }
        file.write(reinterpret_cast<const char*>(&expiry), sizeof(int64_t));
        
        if (!file.good()) {
            return false;
        }
    }
    
    return true;
}

// LOAD_FROM_RDB reads the store from a file in binary RDB format
// Returns true on success, false on error
bool KVStore::load_from_rdb(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Read number of keys
    uint32_t num_keys = 0;
    file.read(reinterpret_cast<char*>(&num_keys), sizeof(uint32_t));
    
    if (!file.good() || file.gcount() != sizeof(uint32_t)) {
        return false;
    }
    
    // Read each key-value pair
    for (uint32_t i = 0; i < num_keys; ++i) {
        // Read key length
        uint32_t key_len = 0;
        file.read(reinterpret_cast<char*>(&key_len), sizeof(uint32_t));
        if (!file.good() || file.gcount() != sizeof(uint32_t)) {
            return false;
        }
        
        // Read key bytes
        std::string key(key_len, '\0');
        file.read(&key[0], key_len);
        if (!file.good() || file.gcount() != key_len) {
            return false;
        }
        
        // Read value length
        uint32_t value_len = 0;
        file.read(reinterpret_cast<char*>(&value_len), sizeof(uint32_t));
        if (!file.good() || file.gcount() != sizeof(uint32_t)) {
            return false;
        }
        
        // Read value bytes
        std::string value(value_len, '\0');
        file.read(&value[0], value_len);
        if (!file.good() || file.gcount() != value_len) {
            return false;
        }
        
        // Read expiration timestamp
        int64_t expiry = 0;
        file.read(reinterpret_cast<char*>(&expiry), sizeof(int64_t));
        if (!file.good() || file.gcount() != sizeof(int64_t)) {
            return false;
        }
        
        // Store key-value
        store_[key] = value;
        update_lru(key);
        
        // Set expiration if timestamp > 0
        if (expiry > 0) {
            time_t now = time(nullptr);
            if (expiry > now) {
                expirations_[key] = static_cast<time_t>(expiry);
            }
        }
    }
    
    evict_if_needed();
    return true;
}
