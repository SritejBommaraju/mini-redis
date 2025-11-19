// Internal shared types and declarations for server implementations
// Used by both tcp_server.cpp and iocp_server.cpp

#pragma once

#include <string>
#include <vector>
#include <set>
#include <map>
#include <mutex>
#include <atomic>
#include <ctime>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#else
#error "This implementation currently supports only Windows (Winsock)."
#endif

class KVStore;

// Forward declaration - include resp_parser.hpp where RespParser is used
class RespParser;

namespace mini_redis {
namespace detail {

// Client context: tracks per-client state (database selection, authentication, request count)
struct ClientContext {
    int db_index = 0; // Current database index
    bool authenticated = false; // Authentication status (stub: always true for now)
    int request_count = 0; // Number of requests processed
    std::set<std::string> subscribed_channels; // Channels this client is subscribed to
    RespParser* parser = nullptr; // RESP parser instance (owned by this context)
    
    // Constructor/destructor implemented in .cpp files (need full RespParser definition)
    ClientContext();
    ~ClientContext();
    
    // Non-copyable (parser ownership)
    ClientContext(const ClientContext&) = delete;
    ClientContext& operator=(const ClientContext&) = delete;
    
    // Movable
    ClientContext(ClientContext&& other) noexcept 
        : db_index(other.db_index), authenticated(other.authenticated),
          request_count(other.request_count), subscribed_channels(std::move(other.subscribed_channels)),
          parser(other.parser) {
        other.parser = nullptr;
    }
};

// Process a single command and return reply and quit flag
struct CommandResult {
    std::string reply;
    bool should_quit;
    bool success;
};

// Shared server resources (defined in tcp_server.cpp)
extern std::vector<KVStore> databases;
extern std::mutex databases_mutex;
extern std::map<std::string, std::set<SOCKET>> channels;
extern std::mutex channels_mutex;
extern time_t server_start_time;
extern std::atomic<long long> total_commands_processed;

} // namespace detail

} // namespace mini_redis

// Forward declarations for global managers (defined in tcp_server.cpp)
// Note: AOFLogger and ReplicationManager are global classes, not in mini_redis namespace
class AOFLogger;
class ReplicationManager;

namespace mini_redis {
// Global manager pointers (defined in tcp_server.cpp)
extern AOFLogger* g_aof_logger;
extern ReplicationManager* g_replication_manager;
} // namespace mini_redis

