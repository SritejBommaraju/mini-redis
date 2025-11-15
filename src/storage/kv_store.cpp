#include "storage/kv_store.hpp"

namespace mini_redis {

void KvStore::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_[key] = value;
}

std::string KvStore::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.find(key);
    if (it == data_.end()) {
        return ""; // key not found
    }
    return it->second;
}

bool KvStore::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return data_.erase(key) > 0;
}

} // namespace mini_redis
