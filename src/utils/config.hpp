// Configuration parsing for Mini-Redis
// Supports command-line args and config file

#ifndef MINI_REDIS_CONFIG_HPP
#define MINI_REDIS_CONFIG_HPP

#include <string>

namespace mini_redis {

struct Config {
    int port = 6379;
    int max_keys = 10000;
    std::string aof_path = "mini_redis.aof";
    std::string rdb_path = "mini_redis_dump.rdb";
    bool use_iocp = false;
};

// Parse command-line arguments
Config parse_args(int argc, char* argv[]);

// Load config from file (returns default if file not found)
Config load_config_file(const std::string& path);

} // namespace mini_redis

#endif // MINI_REDIS_CONFIG_HPP
