// RESP protocol serialization utilities
// Shared between tcp_server and iocp_server

#ifndef MINI_REDIS_RESP_UTILS_HPP
#define MINI_REDIS_RESP_UTILS_HPP

#include <string>
#include <vector>

namespace mini_redis {

// Simple string: +msg\r\n
std::string resp_simple(const std::string& msg);

// Bulk string: $len\r\nmsg\r\n
std::string resp_bulk(const std::string& msg);

// Nil: $-1\r\n
std::string resp_nil();

// Integer: :value\r\n
std::string resp_integer(int value);

// Integer (64-bit): :value\r\n
std::string resp_integer64(int64_t value);

// Array: *count\r\n followed by bulk strings
std::string resp_array(const std::vector<std::string>& items);

// Error: -msg\r\n
std::string resp_err(const std::string& msg);

} // namespace mini_redis

#endif // MINI_REDIS_RESP_UTILS_HPP
