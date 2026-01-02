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
            // Key has expired, remove it from both stores
            store_.erase(key);
            hash_store_.erase(key);
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
    size_t current_size = store_.size() + hash_store_.size();
    while (current_size > MAX_KEYS && !lru_order_.empty()) {
        // Remove oldest key (from back of list)
        std::string oldest_key = lru_order_.back();
        store_.erase(oldest_key);
        hash_store_.erase(oldest_key);
        expirations_.erase(oldest_key);
        lru_map_.erase(oldest_key);
        lru_order_.pop_back();
        current_size = store_.size() + hash_store_.size();
    }
}

void KVStore::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    check_and_remove_expired(key);
    // If it was a hash, remove it (overwrite)
    hash_store_.erase(key);
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
    bool removed = false;

    // Check string store
    auto it = store_.find(key);
    if (it != store_.end()) {
        store_.erase(it);
        removed = true;
    }

    // Check hash store
    auto hit = hash_store_.find(key);
    if (hit != hash_store_.end()) {
        hash_store_.erase(hit);
        removed = true;
    }

    if (removed) {
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
    if (store_.find(key) != store_.end()) return true;
    if (hash_store_.find(key) != hash_store_.end()) return true;
    return false;
}

// KEYS returns all keys currently in the store
// Returns a vector containing all key names
std::vector<std::string> KVStore::keys() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    result.reserve(store_.size() + hash_store_.size());
    // Clean expired keys first (check both stores)
    std::vector<std::string> keys_to_check;
    for (const auto& pair : store_) {
        keys_to_check.push_back(pair.first);
    }
    for (const auto& pair : hash_store_) {
        keys_to_check.push_back(pair.first);
    }
    for (const auto& key : keys_to_check) {
        check_and_remove_expired(key);
    }
    // Now collect remaining keys
    for (const auto& pair : store_) {
        result.push_back(pair.first);
    }
    for (const auto& pair : hash_store_) {
        result.push_back(pair.first);
    }
    return result;
}

// EXPIRE sets expiration time for a key in seconds
// Returns true if key exists and expiration was set, false if key doesn't exist
bool KVStore::expire(const std::string& key, int seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    check_and_remove_expired(key);

    bool exists = (store_.find(key) != store_.end()) || (hash_store_.find(key) != hash_store_.end());
    if (!exists) {
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

    bool exists = (store_.find(key) != store_.end()) || (hash_store_.find(key) != hash_store_.end());
    if (!exists) {
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
    return store_.size() + hash_store_.size();
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
//   [type: uint8]
//   If Type 0 (String): [key_len: uint32][key_bytes][value_len: uint32][value_bytes][expiry: int64]
//   If Type 1 (Hash):   [key_len: uint32][key_bytes][num_fields: uint32] -> for each field: [field_len][field][val_len][val] -> [expiry: int64]
// Returns true on success, false on error
bool KVStore::save_to_rdb(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Clean expired keys first to avoid saving them
    std::vector<std::string> keys_to_check;
    for (const auto& pair : store_) keys_to_check.push_back(pair.first);
    for (const auto& pair : hash_store_) keys_to_check.push_back(pair.first);

    for (const auto& key : keys_to_check) {
        const_cast<KVStore*>(this)->check_and_remove_expired(key);
    }
    
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    
    // Write total number of keys (strings + hashes)
    uint32_t num_keys = static_cast<uint32_t>(store_.size() + hash_store_.size());
    file.write(reinterpret_cast<const char*>(&num_keys), sizeof(uint32_t));
    
    if (!file.good()) return false;
    
    // Write String Keys (Type 0)
    for (const auto& pair : store_) {
        const std::string& key = pair.first;
        const std::string& value = pair.second;
        
        uint8_t type = 0;
        file.write(reinterpret_cast<const char*>(&type), sizeof(uint8_t));

        uint32_t key_len = static_cast<uint32_t>(key.size());
        file.write(reinterpret_cast<const char*>(&key_len), sizeof(uint32_t));
        file.write(key.c_str(), key_len);
        
        uint32_t value_len = static_cast<uint32_t>(value.size());
        file.write(reinterpret_cast<const char*>(&value_len), sizeof(uint32_t));
        file.write(value.c_str(), value_len);
        
        int64_t expiry = 0;
        auto exp_it = expirations_.find(key);
        if (exp_it != expirations_.end()) {
            expiry = static_cast<int64_t>(exp_it->second);
        }
        file.write(reinterpret_cast<const char*>(&expiry), sizeof(int64_t));
        
        if (!file.good()) return false;
    }

    // Write Hash Keys (Type 1)
    for (const auto& pair : hash_store_) {
        const std::string& key = pair.first;
        const auto& fields = pair.second;

        uint8_t type = 1;
        file.write(reinterpret_cast<const char*>(&type), sizeof(uint8_t));

        uint32_t key_len = static_cast<uint32_t>(key.size());
        file.write(reinterpret_cast<const char*>(&key_len), sizeof(uint32_t));
        file.write(key.c_str(), key_len);

        uint32_t num_fields = static_cast<uint32_t>(fields.size());
        file.write(reinterpret_cast<const char*>(&num_fields), sizeof(uint32_t));

        for (const auto& field_pair : fields) {
            const std::string& field = field_pair.first;
            const std::string& val = field_pair.second;

            uint32_t f_len = static_cast<uint32_t>(field.size());
            file.write(reinterpret_cast<const char*>(&f_len), sizeof(uint32_t));
            file.write(field.c_str(), f_len);

            uint32_t v_len = static_cast<uint32_t>(val.size());
            file.write(reinterpret_cast<const char*>(&v_len), sizeof(uint32_t));
            file.write(val.c_str(), v_len);
        }

        int64_t expiry = 0;
        auto exp_it = expirations_.find(key);
        if (exp_it != expirations_.end()) {
            expiry = static_cast<int64_t>(exp_it->second);
        }
        file.write(reinterpret_cast<const char*>(&expiry), sizeof(int64_t));

        if (!file.good()) return false;
    }
    
    return true;
}

// LOAD_FROM_RDB reads the store from a file in binary RDB format
// Returns true on success, false on error
bool KVStore::load_from_rdb(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    
    uint32_t num_keys = 0;
    file.read(reinterpret_cast<char*>(&num_keys), sizeof(uint32_t));
    
    if (!file.good()) return false;

    // Note: We need to guess format version.
    // The previous format started directly with key_len (uint32).
    // The new format starts with type (uint8).
    // However, if the first byte is 0, it could be a type 0 OR the first byte of a uint32 key_len (if little endian and len < 256).
    // This makes backward compatibility hard without a version header.
    // Since we are changing the implementation, we assume the file is in the new format OR we try to detect.
    // But since the task allows breaking changes ("Mini-Redis"), we will just implement the loader for the new format.
    // If the file is old, it will likely fail to load or load garbage.

    // Actually, we should clear the store first
    store_.clear();
    hash_store_.clear();
    expirations_.clear();
    lru_order_.clear();
    lru_map_.clear();
    
    for (uint32_t i = 0; i < num_keys; ++i) {
        uint8_t type = 0;
        file.read(reinterpret_cast<char*>(&type), sizeof(uint8_t));
        if (!file.good()) return false;

        // Read key
        uint32_t key_len = 0;
        file.read(reinterpret_cast<char*>(&key_len), sizeof(uint32_t));
        if (!file.good()) return false;
        
        std::string key(key_len, '\0');
        file.read(&key[0], key_len);
        if (!file.good()) return false;
        
        if (type == 0) { // String
            uint32_t val_len = 0;
            file.read(reinterpret_cast<char*>(&val_len), sizeof(uint32_t));
            if (!file.good()) return false;

            std::string value(val_len, '\0');
            file.read(&value[0], val_len);
            if (!file.good()) return false;

            store_[key] = value;
        } else if (type == 1) { // Hash
            uint32_t num_fields = 0;
            file.read(reinterpret_cast<char*>(&num_fields), sizeof(uint32_t));
            if (!file.good()) return false;

            for (uint32_t j = 0; j < num_fields; ++j) {
                uint32_t f_len = 0;
                file.read(reinterpret_cast<char*>(&f_len), sizeof(uint32_t));
                std::string field(f_len, '\0');
                file.read(&field[0], f_len);

                uint32_t v_len = 0;
                file.read(reinterpret_cast<char*>(&v_len), sizeof(uint32_t));
                std::string val(v_len, '\0');
                file.read(&val[0], v_len);

                hash_store_[key][field] = val;
            }
        } else {
            // Unknown type
            return false;
        }
        
        update_lru(key);
        
        // Read expiry
        int64_t expiry = 0;
        file.read(reinterpret_cast<char*>(&expiry), sizeof(int64_t));
        if (!file.good()) return false;
        
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

KVStore::KeyType KVStore::type(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    check_and_remove_expired(key);
    if (store_.find(key) != store_.end()) return KeyType::String;
    if (hash_store_.find(key) != hash_store_.end()) return KeyType::Hash;
    return KeyType::None;
}

int KVStore::hset(const std::string& key, const std::string& field, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    check_and_remove_expired(key);
    // If it was a string, remove it (overwrite)
    store_.erase(key);

    int is_new = 1;
    if (hash_store_[key].find(field) != hash_store_[key].end()) {
        is_new = 0;
    }

    hash_store_[key][field] = value;
    update_lru(key);
    evict_if_needed();
    return is_new;
}

bool KVStore::hget(const std::string& key, const std::string& field, std::string& outValue) {
    std::lock_guard<std::mutex> lock(mutex_);
    check_and_remove_expired(key);
    auto key_it = hash_store_.find(key);
    if (key_it == hash_store_.end()) {
        return false;
    }
    auto field_it = key_it->second.find(field);
    if (field_it == key_it->second.end()) {
        return false;
    }
    outValue = field_it->second;
    update_lru(key);
    return true;
}
