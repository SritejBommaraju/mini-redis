// TCP server implementation for Mini-Redis
// Handles client connections, command parsing, and RESP protocol responses

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#error "This implementation currently supports only Windows (Winsock)."
#endif

#include "server/tcp_server.hpp"
#include "utils/logger.hpp"

#include "../protocol/resp_utils.hpp"
#include "../protocol/parser.hpp"
#include "../protocol/resp_parser.hpp"
#include "../storage/kv_store.hpp"
#include "../storage/aof_logger.hpp"
#include "replication.hpp"

#include <string>
#include <thread>
#include <vector>
#include <map>
#include <set>
#include <ctime>
#include <sstream>
#include <mutex>
#include <atomic>
#include <fstream>

#include "server/server_common.hpp"

namespace mini_redis {

namespace {

std::mutex stats_mutex; // Keep this local for now

} // anonymous namespace

// Global AOF logger instance (initialized in start_server)
// Declared in server_common.hpp
AOFLogger* mini_redis::g_aof_logger = nullptr;

// Global replication manager instance (initialized in start_server)
// Declared in server_common.hpp
ReplicationManager* mini_redis::g_replication_manager = nullptr;

// Multiple databases: each database is a separate KVStore instance
std::vector<KVStore> mini_redis::detail::databases(16); // Default 16 databases (0-15)
std::mutex mini_redis::detail::databases_mutex; // Protects database access

// Pub/Sub: channel -> set of client sockets subscribed to that channel
std::map<std::string, std::set<SOCKET>> mini_redis::detail::channels;
std::mutex mini_redis::detail::channels_mutex; // Protects channel subscriptions

// Server statistics
time_t mini_redis::detail::server_start_time = time(nullptr);
std::atomic<long long> mini_redis::detail::total_commands_processed{0};

// ClientContext constructor/destructor implementations
mini_redis::detail::ClientContext::ClientContext() {
    parser = new RespParser();
}

mini_redis::detail::ClientContext::~ClientContext() {
    if (parser) {
        delete parser;
        parser = nullptr;
    }
}

// Get current database for a client - made accessible for IOCP server
KVStore& get_db(mini_redis::detail::ClientContext& ctx) {
    std::lock_guard<std::mutex> lock(mini_redis::detail::databases_mutex);
    if (ctx.db_index < 0 || ctx.db_index >= static_cast<int>(mini_redis::detail::databases.size())) {
        ctx.db_index = 0; // Default to database 0 if invalid
    }
    return mini_redis::detail::databases[ctx.db_index];
}

// Extract complete RESP commands using the parser in ClientContext
// Returns vector of parsed command arrays and error message if any
// Made accessible for IOCP server
std::vector<std::vector<std::string>> extract_resp_commands(RespParser* parser, std::string* error_msg = nullptr) {
    std::vector<std::vector<std::string>> commands;
    
    if (!parser) {
        if (error_msg) {
            *error_msg = "ERR parser not initialized";
        }
        return commands;
    }
    
    // Parse all complete RESP commands
    while (true) {
        RespResult result = parser->parse();
        
        if (!result.complete) {
            // Incomplete - need more data
            break;
        }
        
        if (!result.error.empty()) {
            // Parse error
            if (error_msg) {
                *error_msg = result.error;
            }
            break;
        }
        
        // Got a complete command
        if (!result.command.empty()) {
            commands.push_back(std::move(result.command));
        }
        // Continue parsing (may have multiple commands pipelined)
    }
    
    return commands;
}

// Process a single command and return reply and quit flag
// This extracts the command handling logic so it can be called for each pipelined command
// Made accessible for IOCP server
mini_redis::detail::CommandResult process_command(const protocol::Command& cmd, mini_redis::detail::ClientContext& ctx, SOCKET client_socket) {
    mini_redis::detail::CommandResult result;
    result.should_quit = false;
    result.success = false;
    
    ctx.request_count++;
    mini_redis::detail::total_commands_processed++;
    
    // AUTH stub: for now, accept any password or skip if not required
    if (cmd.type == protocol::CommandType::AUTH) {
        ctx.authenticated = true;
        result.reply = resp_simple("OK");
        result.success = true;
        return result;
    }
    
    KVStore& kv = get_db(ctx);
    
    switch (cmd.type) {
        case protocol::CommandType::PING:
            result.reply = resp_simple("PONG");
            result.success = true;
            break;

        case protocol::CommandType::ECHO:
            if (cmd.args.empty()) {
                result.reply = resp_err("ECHO requires a message");
                result.success = false;
            } else {
                result.reply = resp_bulk(cmd.args[0]);
                result.success = true;
            }
            break;

        case protocol::CommandType::SET:
            if (cmd.args.size() < 2) {
                result.reply = resp_err("SET requires key and value");
                result.success = false;
            } else {
                kv.set(cmd.args[0], cmd.args[1]);
                result.reply = resp_simple("OK");
                result.success = true;
                // Log to AOF if enabled
                if (mini_redis::g_aof_logger) {
                    mini_redis::g_aof_logger->append(cmd);
                }
                // Replicate to replicas if enabled
                if (mini_redis::g_replication_manager) {
                    mini_redis::g_replication_manager->replicate_command(cmd);
                }
            }
            break;

        case protocol::CommandType::GET:
            if (cmd.args.empty()) {
                result.reply = resp_err("GET requires a key");
                result.success = false;
            } else {
                std::string value;
                if (kv.get(cmd.args[0], value)) {
                    result.reply = resp_bulk(value);
                    result.success = true;
                } else {
                    result.reply = resp_nil();
                    result.success = true; // Key not found is valid
                }
            }
            break;

        case protocol::CommandType::DEL:
            if (cmd.args.empty()) {
                result.reply = resp_err("DEL requires a key");
                result.success = false;
            } else {
                bool removed = kv.del(cmd.args[0]);
                result.reply = resp_integer(removed ? 1 : 0);
                result.success = true;
                // Log to AOF if enabled and key was removed
                if (mini_redis::g_aof_logger && removed) {
                    mini_redis::g_aof_logger->append(cmd);
                }
                // Replicate to replicas if enabled and key was removed
                if (mini_redis::g_replication_manager && removed) {
                    mini_redis::g_replication_manager->replicate_command(cmd);
                }
            }
            break;

        case protocol::CommandType::EXISTS:
            if (cmd.args.empty()) {
                result.reply = resp_err("EXISTS requires a key");
                result.success = false;
            } else {
                bool exists = kv.exists(cmd.args[0]);
                result.reply = resp_integer(exists ? 1 : 0);
                result.success = true;
            }
            break;

        case protocol::CommandType::KEYS:
            if (cmd.args.empty() || cmd.args[0] != "*") {
                result.reply = resp_err("KEYS only supports wildcard *");
                result.success = false;
            } else {
                std::vector<std::string> all_keys = kv.keys();
                result.reply = resp_array(all_keys);
                result.success = true;
            }
            break;

        case protocol::CommandType::EXPIRE:
            if (cmd.args.size() < 2) {
                result.reply = resp_err("EXPIRE requires key and seconds");
                result.success = false;
            } else {
                try {
                    int seconds = std::stoi(cmd.args[1]);
                    bool set = kv.expire(cmd.args[0], seconds);
                    result.reply = resp_integer(set ? 1 : 0);
                    result.success = true;
                    // Log to AOF if enabled and expiration was set
                    if (mini_redis::g_aof_logger && set) {
                        mini_redis::g_aof_logger->append(cmd);
                    }
                    // Replicate to replicas if enabled and expiration was set
                    if (mini_redis::g_replication_manager && set) {
                        mini_redis::g_replication_manager->replicate_command(cmd);
                    }
                } catch (...) {
                    result.reply = resp_err("Invalid seconds value");
                    result.success = false;
                }
            }
            break;

        case protocol::CommandType::TTL:
            if (cmd.args.empty()) {
                result.reply = resp_err("TTL requires a key");
                result.success = false;
            } else {
                int ttl_value = kv.ttl(cmd.args[0]);
                result.reply = resp_integer(ttl_value);
                result.success = true;
            }
            break;

        case protocol::CommandType::MGET:
            if (cmd.args.empty()) {
                result.reply = resp_err("MGET requires at least one key");
                result.success = false;
            } else {
                std::vector<std::string> values;
                for (const auto& key : cmd.args) {
                    std::string value;
                    if (kv.get(key, value)) {
                        values.push_back(value);
                    } else {
                        values.push_back("");
                    }
                }
                std::string mget_result = "*" + std::to_string(values.size()) + "\r\n";
                for (const auto& val : values) {
                    if (val.empty()) {
                        mget_result += resp_nil();
                    } else {
                        mget_result += resp_bulk(val);
                    }
                }
                result.reply = mget_result;
                result.success = true;
            }
            break;

        case protocol::CommandType::QUIT:
            result.reply = resp_simple("OK");
            result.should_quit = true;
            result.success = true;
            break;

        case protocol::CommandType::SAVE:
            // SAVE writes current database to RDB file
            if (kv.save_to_rdb("mini_redis_dump.rdb")) {
                result.reply = resp_simple("OK");
                result.success = true;
            } else {
                result.reply = resp_err("ERR Save failed");
                result.success = false;
            }
            break;

        case protocol::CommandType::LOAD:
            // LOAD reads database from RDB file
            if (kv.load_from_rdb("mini_redis_dump.rdb")) {
                result.reply = resp_simple("OK");
                result.success = true;
            } else {
                result.reply = resp_err("ERR Load failed");
                result.success = false;
            }
            break;

        case protocol::CommandType::SELECT:
            if (cmd.args.empty()) {
                result.reply = resp_err("SELECT requires database number");
                result.success = false;
            } else {
                try {
                    int db_num = std::stoi(cmd.args[0]);
                    std::lock_guard<std::mutex> lock(mini_redis::detail::databases_mutex);
                    if (db_num >= 0 && db_num < static_cast<int>(mini_redis::detail::databases.size())) {
                        ctx.db_index = db_num;
                        result.reply = resp_simple("OK");
                        result.success = true;
                    } else {
                        result.reply = resp_err("Database index out of range");
                        result.success = false;
                    }
                } catch (...) {
                    result.reply = resp_err("Invalid database number");
                    result.success = false;
                }
            }
            break;

        case protocol::CommandType::INFO: {
            time_t now = time(nullptr);
            long long uptime = now - mini_redis::detail::server_start_time;
            std::lock_guard<std::mutex> lock(mini_redis::detail::databases_mutex);
            size_t total_keys = 0;
            for (const auto& db : mini_redis::detail::databases) {
                total_keys += db.size();
            }
            std::stringstream info;
            info << "uptime:" << uptime << "\n";
            info << "total_keys:" << total_keys << "\n";
            info << "commands_processed:" << mini_redis::detail::total_commands_processed.load() << "\n";
            info << "databases:" << mini_redis::detail::databases.size() << "\n";
            result.reply = resp_bulk(info.str());
            result.success = true;
            break;
        }

        case protocol::CommandType::SUBSCRIBE:
            if (cmd.args.empty()) {
                result.reply = resp_err("SUBSCRIBE requires channel name");
                result.success = false;
            } else {
                std::lock_guard<std::mutex> lock(mini_redis::detail::channels_mutex);
                for (const auto& channel : cmd.args) {
                    mini_redis::detail::channels[channel].insert(client_socket);
                    ctx.subscribed_channels.insert(channel);
                }
                result.reply = resp_simple("OK");
                result.success = true;
            }
            break;

        case protocol::CommandType::PUBLISH:
            if (cmd.args.size() < 2) {
                result.reply = resp_err("PUBLISH requires channel and message");
                result.success = false;
            } else {
                std::lock_guard<std::mutex> lock(mini_redis::detail::channels_mutex);
                const std::string& channel = cmd.args[0];
                const std::string& message = cmd.args[1];
                auto it = mini_redis::detail::channels.find(channel);
                int subscribers = 0;
                if (it != mini_redis::detail::channels.end()) {
                    for (SOCKET sub_socket : it->second) {
                        std::string pub_msg = resp_array({channel, message});
                        send(sub_socket, pub_msg.c_str(), static_cast<int>(pub_msg.size()), 0);
                        subscribers++;
                    }
                }
                result.reply = resp_integer(subscribers);
                result.success = true;
            }
            break;

        case protocol::CommandType::EVAL:
            result.reply = resp_err("ERR Scripting not implemented");
            result.success = false;
            break;

        case protocol::CommandType::AUTH:
            // Already handled above
            break;

        case protocol::CommandType::INCR:
            if (cmd.args.empty()) {
                result.reply = resp_err("ERR INCR requires a key");
                result.success = false;
            } else {
                auto [val, err] = kv.incr(cmd.args[0]);
                if (!err.empty()) {
                    result.reply = resp_err(err);
                    result.success = false;
                } else {
                    result.reply = resp_integer64(val);
                    result.success = true;
                }
            }
            break;

        case protocol::CommandType::DECR:
            if (cmd.args.empty()) {
                result.reply = resp_err("ERR DECR requires a key");
                result.success = false;
            } else {
                auto [val, err] = kv.decr(cmd.args[0]);
                if (!err.empty()) {
                    result.reply = resp_err(err);
                    result.success = false;
                } else {
                    result.reply = resp_integer64(val);
                    result.success = true;
                }
            }
            break;

        case protocol::CommandType::INCRBY:
            if (cmd.args.size() < 2) {
                result.reply = resp_err("ERR INCRBY requires key and increment");
                result.success = false;
            } else {
                try {
                    int64_t delta = std::stoll(cmd.args[1]);
                    auto [val, err] = kv.incrby(cmd.args[0], delta);
                    if (!err.empty()) {
                        result.reply = resp_err(err);
                        result.success = false;
                    } else {
                        result.reply = resp_integer64(val);
                        result.success = true;
                    }
                } catch (...) {
                    result.reply = resp_err("ERR value is not an integer");
                    result.success = false;
                }
            }
            break;

        case protocol::CommandType::DECRBY:
            if (cmd.args.size() < 2) {
                result.reply = resp_err("ERR DECRBY requires key and decrement");
                result.success = false;
            } else {
                try {
                    int64_t delta = std::stoll(cmd.args[1]);
                    auto [val, err] = kv.decrby(cmd.args[0], delta);
                    if (!err.empty()) {
                        result.reply = resp_err(err);
                        result.success = false;
                    } else {
                        result.reply = resp_integer64(val);
                        result.success = true;
                    }
                } catch (...) {
                    result.reply = resp_err("ERR value is not an integer");
                    result.success = false;
                }
            }
            break;

        case protocol::CommandType::APPEND:
            if (cmd.args.size() < 2) {
                result.reply = resp_err("ERR APPEND requires key and value");
                result.success = false;
            } else {
                size_t newlen = kv.append(cmd.args[0], cmd.args[1]);
                result.reply = resp_integer(static_cast<int>(newlen));
                result.success = true;
            }
            break;

        case protocol::CommandType::STRLEN:
            if (cmd.args.empty()) {
                result.reply = resp_err("ERR STRLEN requires a key");
                result.success = false;
            } else {
                size_t len = kv.strlen(cmd.args[0]);
                result.reply = resp_integer(static_cast<int>(len));
                result.success = true;
            }
            break;

        default:
            result.reply = resp_err("Unknown command");
            result.success = false;
            break;
    }
    
    return result;
}

void handle_client(SOCKET client_socket) {
    mini_redis::Logger::log(mini_redis::Logger::Level::Info, "Client connected");
    mini_redis::detail::ClientContext ctx;
    bool should_quit = false;

    char buffer[1024];

    while (!should_quit) {
        int bytes = recv(client_socket, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (bytes <= 0) {
            break;
        }

        // Append new data to parser (handles partial commands)
        // Note: parser maintains its own buffer and handles binary data correctly
        if (ctx.parser) {
            ctx.parser->append(buffer, bytes);
        }

#ifdef DEBUG_LOGGING
        mini_redis::Logger::log(mini_redis::Logger::Level::Info, "Received " + std::to_string(bytes) + " bytes");
#endif

        // Extract all complete RESP commands from parser
        std::string parse_error;
        std::vector<std::vector<std::string>> resp_commands = extract_resp_commands(ctx.parser, &parse_error);
        
        // If we got a parse error and no commands, send error and log details
        if (resp_commands.empty() && !parse_error.empty()) {
            // Log the parse error with buffer context for debugging
            mini_redis::Logger::log(mini_redis::Logger::Level::Warn, "RESP parse error: " + parse_error);
            std::string error_reply = resp_err(parse_error);
            send(client_socket, error_reply.c_str(), static_cast<int>(error_reply.size()), 0);
            // Don't clear buffer here - let it accumulate more data or close connection naturally
            // This allows the connection to recover if the next command is valid
            continue; // Skip to next recv
        }

        // Process each complete command
        for (const auto& resp_array : resp_commands) {
            // Convert RESP array to Command struct
            protocol::Command cmd = protocol::command_from_resp_array(resp_array);
            
            // Handle parse errors
            if (cmd.type == protocol::CommandType::UNKNOWN && !resp_array.empty()) {
                std::string error_reply = resp_err("ERR unknown command '" + resp_array[0] + "'");
                send(client_socket, error_reply.c_str(), static_cast<int>(error_reply.size()), 0);
                continue;
            }
            
            mini_redis::detail::CommandResult result = process_command(cmd, ctx, client_socket);
            
            // Log command result
#ifdef DEBUG_LOGGING
            mini_redis::Logger::log(result.success ? mini_redis::Logger::Level::Info : mini_redis::Logger::Level::Warn,
                       "Command: " + std::to_string(static_cast<int>(cmd.type)) + 
                       (result.success ? " SUCCESS" : " FAILED") + 
                       " (client requests: " + std::to_string(ctx.request_count) + ")");
#endif

            // Send reply for this command
            send(client_socket, result.reply.c_str(), static_cast<int>(result.reply.size()), 0);
            
            // Check if client wants to quit
            if (result.should_quit) {
                should_quit = true;
                break;
            }
        }
    }

    // Cleanup: remove client from all channel subscriptions
    {
        std::lock_guard<std::mutex> lock(mini_redis::detail::channels_mutex);
        for (auto& pair : mini_redis::detail::channels) {
            pair.second.erase(client_socket);
        }
    }

    mini_redis::Logger::log(mini_redis::Logger::Level::Info, "Client disconnected (processed " + 
               std::to_string(ctx.request_count) + " requests)");
    closesocket(client_socket);
}

bool init_winsock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        mini_redis::Logger::log(mini_redis::Logger::Level::Error, "WSAStartup failed with error " + std::to_string(result));
        return false;
    }
    return true;
}

int start_server(int port) {
    if (!init_winsock()) {
        return 1;
    }
    
    // Initialize AOF logger
    static AOFLogger aof_logger("mini_redis.aof");
    mini_redis::g_aof_logger = &aof_logger;
    mini_redis::g_aof_logger->start();
    
    // Replay AOF file on startup if it exists
    std::ifstream aof_check("mini_redis.aof");
    if (aof_check.good()) {
        aof_check.close();
        if (mini_redis::g_aof_logger->replay(mini_redis::detail::databases[0])) {
            mini_redis::Logger::log(mini_redis::Logger::Level::Info, "AOF file replayed successfully");
        }
    }
    
    // Initialize replication manager
    static ReplicationManager replication_manager;
    mini_redis::g_replication_manager = &replication_manager;
    mini_redis::g_replication_manager->start();

    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET) {
        mini_redis::Logger::log(mini_redis::Logger::Level::Error, "socket() failed");
        WSACleanup();
        return 1;
    }

    char yes = 1;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        mini_redis::Logger::log(mini_redis::Logger::Level::Error, "bind() failed");
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        mini_redis::Logger::log(mini_redis::Logger::Level::Error, "listen() failed");
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    mini_redis::Logger::log(mini_redis::Logger::Level::Info, "Mini-Redis running on port " + std::to_string(port));

    // Main server loop
    while (true) {
        SOCKET client_socket = accept(listen_socket, nullptr, nullptr);
        if (client_socket == INVALID_SOCKET) {
            mini_redis::Logger::log(mini_redis::Logger::Level::Error, "accept() failed");
            continue;
        }

        std::thread t(handle_client, client_socket);
        t.detach();
    }

    closesocket(listen_socket);
    WSACleanup();
    return 0;
}

} // namespace mini_redis

