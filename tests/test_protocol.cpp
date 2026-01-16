// Unit tests for Mini-Redis parser and KVStore
// CPU-side tests only, no networking dependencies

#include "../src/protocol/parser.hpp"
#include "../src/protocol/resp_parser.hpp"
#include "../src/storage/kv_store.hpp"
#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>

void test_parser() {
    std::cout << "Testing parser...\n";

    // Test PING
    auto cmd = protocol::parse_command("PING");
    assert(cmd.type == protocol::CommandType::PING);
    assert(cmd.args.empty());

    // Test ECHO
    cmd = protocol::parse_command("ECHO hello");
    assert(cmd.type == protocol::CommandType::ECHO);
    assert(cmd.args.size() == 1);
    assert(cmd.args[0] == "hello");

    // Test SET
    cmd = protocol::parse_command("SET key value");
    assert(cmd.type == protocol::CommandType::SET);
    assert(cmd.args.size() == 2);
    assert(cmd.args[0] == "key");
    assert(cmd.args[1] == "value");

    // Test GET
    cmd = protocol::parse_command("GET key");
    assert(cmd.type == protocol::CommandType::GET);
    assert(cmd.args.size() == 1);

    // Test DEL
    cmd = protocol::parse_command("DEL key");
    assert(cmd.type == protocol::CommandType::DEL);

    // Test EXISTS
    cmd = protocol::parse_command("EXISTS key");
    assert(cmd.type == protocol::CommandType::EXISTS);

    // Test KEYS
    cmd = protocol::parse_command("KEYS *");
    assert(cmd.type == protocol::CommandType::KEYS);
    assert(cmd.args.size() == 1);
    assert(cmd.args[0] == "*");

    // Test case insensitivity
    cmd = protocol::parse_command("ping");
    assert(cmd.type == protocol::CommandType::PING);

    cmd = protocol::parse_command("set KEY VALUE");
    assert(cmd.type == protocol::CommandType::SET);
    assert(cmd.args[0] == "KEY");
    assert(cmd.args[1] == "VALUE");

    // Test unknown command
    cmd = protocol::parse_command("UNKNOWN cmd");
    assert(cmd.type == protocol::CommandType::UNKNOWN);

    std::cout << "Parser tests passed!\n";
}

void test_resp_parser() {
    std::cout << "Testing RESP parser...\n";

    // Test 1: Simple PING command - *1\r\n$4\r\nPING\r\n
    {
        RespParser parser;
        const char* data = "*1\r\n$4\r\nPING\r\n";
        parser.append(data, strlen(data));
        RespResult result = parser.parse();
        assert(result.complete);
        assert(result.error.empty());
        assert(result.command.size() == 1);
        assert(result.command[0] == "PING");
    }

    // Test 2: SET command - *3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n
    {
        RespParser parser;
        const char* data = "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n";
        parser.append(data, strlen(data));
        RespResult result = parser.parse();
        assert(result.complete);
        assert(result.error.empty());
        assert(result.command.size() == 3);
        assert(result.command[0] == "SET");
        assert(result.command[1] == "key");
        assert(result.command[2] == "value");
    }

    // Test 3: GET command - *2\r\n$3\r\nGET\r\n$3\r\nkey\r\n
    {
        RespParser parser;
        const char* data = "*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n";
        parser.append(data, strlen(data));
        RespResult result = parser.parse();
        assert(result.complete);
        assert(result.error.empty());
        assert(result.command.size() == 2);
        assert(result.command[0] == "GET");
        assert(result.command[1] == "key");
    }

    // Test 4: Empty array
    {
        RespParser parser;
        const char* data = "*0\r\n";
        parser.append(data, strlen(data));
        RespResult result = parser.parse();
        assert(result.complete);
        assert(result.error.empty());
        assert(result.command.empty());
    }

    // Test 5: Incomplete command (should return incomplete)
    {
        RespParser parser;
        const char* data = "*1\r\n$4\r\nPIN"; // Missing G\r\n
        parser.append(data, strlen(data));
        RespResult result = parser.parse();
        assert(!result.complete);
        assert(result.error.empty());
    }

    // Test 6: Multiple commands in one buffer (pipelining)
    {
        RespParser parser;
        const char* data = "*1\r\n$4\r\nPING\r\n*1\r\n$4\r\nPING\r\n";
        parser.append(data, strlen(data));
        RespResult result1 = parser.parse();
        assert(result1.complete);
        assert(result1.error.empty());
        assert(result1.command.size() == 1);
        assert(result1.command[0] == "PING");
        
        // Parse second command
        RespResult result2 = parser.parse();
        assert(result2.complete);
        assert(result2.error.empty());
        assert(result2.command.size() == 1);
        assert(result2.command[0] == "PING");
    }

    // Test 7: Command name case conversion (should be uppercase)
    {
        RespParser parser;
        const char* data = "*1\r\n$4\r\nping\r\n";
        parser.append(data, strlen(data));
        RespResult result = parser.parse();
        assert(result.complete);
        assert(result.error.empty());
        assert(result.command.size() == 1);
        assert(result.command[0] == "PING"); // Should be converted to uppercase
    }

    // Test 8: Nil bulk string
    {
        RespParser parser;
        const char* data = "*1\r\n$-1\r\n";
        parser.append(data, strlen(data));
        RespResult result = parser.parse();
        assert(result.complete);
        assert(result.error.empty());
        assert(result.command.size() == 1);
        assert(result.command[0].empty()); // Nil bulk string should be empty
    }

    std::cout << "RESP parser tests passed!\n";
}

void test_kvstore() {
    std::cout << "Testing KVStore...\n";

    KVStore kv;

    // Test SET and GET
    kv.set("key1", "value1");
    std::string value;
    assert(kv.get("key1", value));
    assert(value == "value1");

    // Test GET non-existent key
    assert(!kv.get("nonexistent", value));

    // Test EXISTS
    assert(kv.exists("key1"));
    assert(!kv.exists("nonexistent"));

    // Test DEL
    assert(kv.del("key1"));
    assert(!kv.exists("key1"));
    assert(!kv.del("nonexistent"));

    // Test KEYS
    kv.set("key1", "value1");
    kv.set("key2", "value2");
    kv.set("key3", "value3");
    auto keys = kv.keys();
    assert(keys.size() == 3);
    assert(std::find(keys.begin(), keys.end(), "key1") != keys.end());
    assert(std::find(keys.begin(), keys.end(), "key2") != keys.end());
    assert(std::find(keys.begin(), keys.end(), "key3") != keys.end());

    // Test EXPIRE and TTL
    kv.set("expire_key", "value");
    assert(kv.expire("expire_key", 10));
    int ttl = kv.ttl("expire_key");
    assert(ttl > 0 && ttl <= 10);
    assert(!kv.expire("nonexistent", 10));

    // Test TTL for key without expiration
    kv.set("no_expire", "value");
    assert(kv.ttl("no_expire") == -1);

    // Test TTL for non-existent key
    assert(kv.ttl("nonexistent") == -2);

    // Test SIZE
    assert(kv.size() >= 3); // At least the keys we added

    // Test persistence
    kv.set("persist_key", "persist_value");
    kv.save_to_file("test_dump.db");
    
    KVStore kv2;
    kv2.load_from_file("test_dump.db");
    std::string loaded_value;
    assert(kv2.get("persist_key", loaded_value));
    assert(loaded_value == "persist_value");

    std::cout << "KVStore tests passed!\n";
}

// Forward declaration for atomic command tests
extern void run_atomic_command_tests();

// Forward declaration for config tests
extern void run_config_tests();

int main() {
    std::cout << "Running Mini-Redis unit tests...\n\n";
    
    try {
        test_parser();
        test_resp_parser();
        test_kvstore();
        run_atomic_command_tests();
        run_config_tests();
        std::cout << "\nAll tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}



