// RESP protocol serialization utilities

#include "resp_utils.hpp"

namespace mini_redis {

std::string resp_simple(const std::string& msg) {
    return "+" + msg + "\r\n";
}

std::string resp_bulk(const std::string& msg) {
    return "$" + std::to_string(msg.size()) + "\r\n" + msg + "\r\n";
}

std::string resp_nil() {
    return "$-1\r\n";
}

std::string resp_integer(int value) {
    return ":" + std::to_string(value) + "\r\n";
}

std::string resp_integer64(int64_t value) {
    return ":" + std::to_string(value) + "\r\n";
}

std::string resp_array(const std::vector<std::string>& items) {
    std::string result = "*" + std::to_string(items.size()) + "\r\n";
    for (const auto& item : items) {
        result += "$" + std::to_string(item.size()) + "\r\n" + item + "\r\n";
    }
    return result;
}

std::string resp_err(const std::string& msg) {
    return "-" + msg + "\r\n";
}

} // namespace mini_redis
