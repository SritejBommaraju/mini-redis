// Configuration parsing implementation

#include "config.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace mini_redis {

static std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    if (start == s.size()) return "";
    size_t end = s.size() - 1;
    while (end > start && std::isspace(static_cast<unsigned char>(s[end]))) {
        --end;
    }
    return s.substr(start, end - start + 1);
}

Config parse_args(int argc, char* argv[]) {
    Config cfg;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            try {
                cfg.port = std::stoi(argv[++i]);
            } catch (...) {
                // Keep default
            }
        } else if ((arg == "--max-keys" || arg == "-m") && i + 1 < argc) {
            try {
                cfg.max_keys = std::stoi(argv[++i]);
            } catch (...) {
                // Keep default
            }
        } else if ((arg == "--aof" || arg == "-a") && i + 1 < argc) {
            cfg.aof_path = argv[++i];
        } else if ((arg == "--rdb" || arg == "-r") && i + 1 < argc) {
            cfg.rdb_path = argv[++i];
        } else if (arg == "--iocp") {
            cfg.use_iocp = true;
        } else if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            // Load from config file, then apply remaining CLI args
            cfg = load_config_file(argv[++i]);
        }
    }
    
    return cfg;
}

Config load_config_file(const std::string& path) {
    Config cfg;
    
    std::ifstream file(path);
    if (!file.is_open()) {
        return cfg;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        
        if (key == "port") {
            try { cfg.port = std::stoi(value); } catch (...) {}
        } else if (key == "max_keys") {
            try { cfg.max_keys = std::stoi(value); } catch (...) {}
        } else if (key == "aof_path") {
            cfg.aof_path = value;
        } else if (key == "rdb_path") {
            cfg.rdb_path = value;
        } else if (key == "use_iocp") {
            cfg.use_iocp = (value == "true" || value == "1" || value == "yes");
        }
    }
    
    return cfg;
}

} // namespace mini_redis
