#include <unordered_map>
#include <mutex>
#include <string>

class KeyValueStore {
private:
    std::unordered_map<std::string, std::string> store;
    std::mutex mtx;

public:
    std::string set(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mtx);
        store[key] = value;
        return "+OK\r\n";
    }

    std::string get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mtx);
        if (store.find(key) != store.end()) {
            return "$" + std::to_string(store[key].size()) + "\r\n" + store[key] + "\r\n";
        }
        return "$-1\r\n"; // Redis null response
    }
};
