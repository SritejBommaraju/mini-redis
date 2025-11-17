#include "kv_store.hpp"

void KVStore::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    store_[key] = value;
}

bool KVStore::get(const std::string& key, std::string& outValue) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = store_.find(key);
    if (it == store_.end()) {
        return false;
    }
    outValue = it->second;
    return true;
}
