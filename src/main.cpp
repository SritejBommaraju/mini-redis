// Main entry point for Mini-Redis server

#include "server/tcp_server.hpp"
#include "storage/kv_store.hpp"
#include "utils/logger.hpp"
#include "utils/config.hpp"
#include <fstream>
#include <iostream>

void print_usage() {
    std::cout << "Usage: mini_redis [options]\n"
              << "Options:\n"
              << "  -p, --port PORT      Server port (default: 6379)\n"
              << "  -m, --max-keys N     Max keys before LRU eviction (default: 10000)\n"
              << "  -a, --aof PATH       AOF file path (default: mini_redis.aof)\n"
              << "  -r, --rdb PATH       RDB file path (default: mini_redis_dump.rdb)\n"
              << "  -c, --config PATH    Config file path\n"
              << "      --iocp           Use IOCP server (high performance)\n"
              << "  -h, --help           Show this help\n";
}

int main(int argc, char* argv[]) {
    // Check for help flag
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        }
    }
    
    // Parse configuration
    mini_redis::Config cfg = mini_redis::parse_args(argc, argv);
    
    mini_redis::Logger::log(mini_redis::Logger::Level::Info, 
                           "Config: port=" + std::to_string(cfg.port) + 
                           " max_keys=" + std::to_string(cfg.max_keys) +
                           " iocp=" + (cfg.use_iocp ? "true" : "false"));
    
    // Check for persistence file
    std::ifstream check_file(cfg.rdb_path);
    if (check_file.good()) {
        check_file.close();
        mini_redis::Logger::log(mini_redis::Logger::Level::Info, 
                               "Found persistence file (use LOAD command to restore)");
    }

    // Start server
    if (cfg.use_iocp) {
        return mini_redis::start_server_iocp(cfg.port);
    } else {
        return mini_redis::start_server(cfg.port);
    }
}
