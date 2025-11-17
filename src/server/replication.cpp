// Replication manager implementation
// Sends write commands to replica servers

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#error "This implementation currently supports only Windows (Winsock)."
#endif

#include "replication.hpp"
#include "../protocol/parser.hpp"
#include "../utils/logger.hpp"

#include <sstream>
#include <algorithm>
#include <cctype>

// Convert command to RESP array format (shared with AOF logger)
static std::string command_to_resp(const protocol::Command& cmd) {
    std::string result;
    
    // Determine command name from type
    std::string cmd_name;
    switch (cmd.type) {
        case protocol::CommandType::SET: cmd_name = "SET"; break;
        case protocol::CommandType::DEL: cmd_name = "DEL"; break;
        case protocol::CommandType::EXPIRE: cmd_name = "EXPIRE"; break;
        default: return ""; // Only replicate write commands
    }
    
    // Count total arguments (command name + args)
    size_t arg_count = 1 + cmd.args.size();
    
    // Build RESP array: *<count>\r\n
    result = "*" + std::to_string(arg_count) + "\r\n";
    
    // Add command name as bulk string: $<len>\r\n<value>\r\n
    result += "$" + std::to_string(cmd_name.size()) + "\r\n" + cmd_name + "\r\n";
    
    // Add each argument as bulk string
    for (const auto& arg : cmd.args) {
        result += "$" + std::to_string(arg.size()) + "\r\n" + arg + "\r\n";
    }
    
    return result;
}

ReplicationManager::ReplicationManager() {
}

ReplicationManager::~ReplicationManager() {
    stop();
}

void ReplicationManager::start() {
    // Nothing to initialize for now
}

void ReplicationManager::stop() {
    std::lock_guard<std::mutex> lock(replicas_mutex_);
    
    // Close all replica connections
    for (auto& replica : replicas_) {
        if (replica.connected && replica.socket != nullptr) {
            closesocket(reinterpret_cast<SOCKET>(replica.socket));
            replica.socket = nullptr;
            replica.connected = false;
        }
    }
    replicas_.clear();
}

void ReplicationManager::add_replica(const std::string& host, int port) {
    std::lock_guard<std::mutex> lock(replicas_mutex_);
    
    // Check if replica already exists
    for (const auto& replica : replicas_) {
        if (replica.host == host && replica.port == port) {
            mini_redis::Logger::log(mini_redis::Logger::Level::Warn, "Replica " + host + ":" + std::to_string(port) + " already exists");
            return;
        }
    }
    
    // Create socket and connect to replica
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        mini_redis::Logger::log(mini_redis::Logger::Level::Error, "Failed to create socket for replica " + host + ":" + std::to_string(port));
        return;
    }
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        mini_redis::Logger::log(mini_redis::Logger::Level::Error, "Failed to connect to replica " + host + ":" + std::to_string(port));
        closesocket(sock);
        return;
    }
    
    // Add to replicas list
    ReplicaEndpoint replica;
    replica.host = host;
    replica.port = port;
    replica.socket = reinterpret_cast<void*>(sock);
    replica.connected = true;
    replicas_.push_back(replica);
    
    mini_redis::Logger::log(mini_redis::Logger::Level::Info, "Connected to replica " + host + ":" + std::to_string(port));
}

void ReplicationManager::remove_replica(const std::string& host, int port) {
    std::lock_guard<std::mutex> lock(replicas_mutex_);
    
    for (auto it = replicas_.begin(); it != replicas_.end(); ++it) {
        if (it->host == host && it->port == port) {
            if (it->connected && it->socket != nullptr) {
                closesocket(reinterpret_cast<SOCKET>(it->socket));
            }
            replicas_.erase(it);
            mini_redis::Logger::log(mini_redis::Logger::Level::Info, "Removed replica " + host + ":" + std::to_string(port));
            return;
        }
    }
}

void ReplicationManager::replicate_command(const protocol::Command& cmd) {
    // Only replicate write commands
    if (cmd.type != protocol::CommandType::SET && 
        cmd.type != protocol::CommandType::DEL && 
        cmd.type != protocol::CommandType::EXPIRE) {
        return;
    }
    
    std::string resp_cmd = command_to_resp(cmd);
    if (resp_cmd.empty()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(replicas_mutex_);
    
    // Send command to all connected replicas
    for (auto& replica : replicas_) {
        if (!replica.connected || replica.socket == nullptr) {
            continue;
        }
        
        SOCKET sock = reinterpret_cast<SOCKET>(replica.socket);
        int sent = send(sock, resp_cmd.c_str(), static_cast<int>(resp_cmd.size()), 0);
        
        if (sent == SOCKET_ERROR || sent != static_cast<int>(resp_cmd.size())) {
            // Connection failed, mark as disconnected
            mini_redis::Logger::log(mini_redis::Logger::Level::Warn, "Failed to send to replica " + replica.host + ":" + std::to_string(replica.port));
            closesocket(sock);
            replica.socket = nullptr;
            replica.connected = false;
        }
    }
}

