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

#include "../protocol/parser.hpp"
#include "../protocol/resp_parser.hpp"
#include "../storage/kv_store.hpp"
#include "../storage/aof_logger.hpp"
#include "replication.hpp"
#include "commands.hpp"

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

// RESP helpers (accessible to process_command and handle_client)
std::string resp_simple(const std::string &msg) {
    return "+" + msg + "\r\n";
}

std::string resp_bulk(const std::string &msg) {
    return "$" + std::to_string(msg.size()) + "\r\n" + msg + "\r\n";
}

std::string resp_nil() {
    return "$-1\r\n";
}

// RESP integer reply: returns ":1\r\n" for true/1, ":0\r\n" for false/0
std::string resp_integer(int value) {
    return ":" + std::to_string(value) + "\r\n";
}

// RESP array serialization: encodes items as "*<count>\r\n" followed by bulk strings
// Each item is encoded as "$<len>\r\n<value>\r\n"
std::string resp_array(const std::vector<std::string>& items) {
    std::string result = "*" + std::to_string(items.size()) + "\r\n";
    for (const auto& item : items) {
        result += "$" + std::to_string(item.size()) + "\r\n" + item + "\r\n";
    }
    return result;
}

std::string resp_err(const std::string &msg) {
    return "-" + msg + "\r\n";
}

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
    ctx.request_count++;
    mini_redis::detail::total_commands_processed++;

    if (cmd.type == protocol::CommandType::EVAL) {
        mini_redis::detail::CommandResult result;
        result.reply = resp_err("ERR Scripting not implemented");
        result.success = false;
        return result;
    }

    // Use Command Factory for commands
    auto handler = mini_redis::commands::CommandFactory::create(cmd.type);
    KVStore& kv = get_db(ctx);
    return handler->execute(cmd, ctx, kv, client_socket);
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

