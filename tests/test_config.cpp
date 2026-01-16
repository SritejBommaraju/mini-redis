// Tests for configuration parsing

#include "../src/utils/config.hpp"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdio>

void test_default_config() {
    std::cout << "Testing default config...\n";
    mini_redis::Config cfg;
    
    assert(cfg.port == 6379);
    assert(cfg.max_keys == 10000);
    assert(cfg.aof_path == "mini_redis.aof");
    assert(cfg.rdb_path == "mini_redis_dump.rdb");
    assert(cfg.use_iocp == false);
    
    std::cout << "Default config tests passed!\n";
}

void test_parse_args_port() {
    std::cout << "Testing --port arg...\n";
    
    char* args[] = {(char*)"mini_redis", (char*)"--port", (char*)"6380"};
    auto cfg = mini_redis::parse_args(3, args);
    assert(cfg.port == 6380);
    
    std::cout << "--port arg tests passed!\n";
}

void test_parse_args_short() {
    std::cout << "Testing short args...\n";
    
    char* args[] = {(char*)"mini_redis", (char*)"-p", (char*)"7000"};
    auto cfg = mini_redis::parse_args(3, args);
    assert(cfg.port == 7000);
    
    std::cout << "Short args tests passed!\n";
}

void test_parse_args_iocp() {
    std::cout << "Testing --iocp flag...\n";
    
    char* args[] = {(char*)"mini_redis", (char*)"--iocp"};
    auto cfg = mini_redis::parse_args(2, args);
    assert(cfg.use_iocp == true);
    
    std::cout << "--iocp flag tests passed!\n";
}

void test_parse_args_multiple() {
    std::cout << "Testing multiple args...\n";
    
    char* args[] = {(char*)"mini_redis", (char*)"--port", (char*)"8000", 
                    (char*)"--max-keys", (char*)"5000", (char*)"--iocp"};
    auto cfg = mini_redis::parse_args(6, args);
    assert(cfg.port == 8000);
    assert(cfg.max_keys == 5000);
    assert(cfg.use_iocp == true);
    
    std::cout << "Multiple args tests passed!\n";
}

void test_config_file() {
    std::cout << "Testing config file...\n";
    
    // Create temp config file
    const char* test_cfg = "test_mini_redis.conf";
    {
        std::ofstream f(test_cfg);
        f << "# Test config\n";
        f << "port = 9000\n";
        f << "max_keys = 20000\n";
        f << "use_iocp = true\n";
    }
    
    auto cfg = mini_redis::load_config_file(test_cfg);
    assert(cfg.port == 9000);
    assert(cfg.max_keys == 20000);
    assert(cfg.use_iocp == true);
    
    // Cleanup
    std::remove(test_cfg);
    
    std::cout << "Config file tests passed!\n";
}

void test_missing_config_file() {
    std::cout << "Testing missing config file...\n";
    
    auto cfg = mini_redis::load_config_file("nonexistent.conf");
    // Should return defaults
    assert(cfg.port == 6379);
    
    std::cout << "Missing config file tests passed!\n";
}

void run_config_tests() {
    test_default_config();
    test_parse_args_port();
    test_parse_args_short();
    test_parse_args_iocp();
    test_parse_args_multiple();
    test_config_file();
    test_missing_config_file();
}
