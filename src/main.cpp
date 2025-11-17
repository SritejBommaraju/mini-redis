// Main entry point for Mini-Redis server
// Initializes the server and starts listening for client connections

#include "server/tcp_server.hpp"
#include "storage/kv_store.hpp"
#include "utils/logger.hpp"
#include <fstream>

int main() {
    // Try to load persisted data on startup (if file exists)
    // Note: This loads into database 0 only, as we don't have access to databases here
    // In a full implementation, we'd load all databases
    std::ifstream check_file("mini_redis_dump.db");
    if (check_file.good()) {
        check_file.close();
        // Database loading is handled per-database via LOAD command
        // For now, we just note that the file exists
        mini_redis::Logger::log(mini_redis::Logger::Level::Info, 
                               "Found persistence file (use LOAD command to restore)");
    }

    // For now we always listen on 6379.
    // Later we can add CLI args like: mini_redis --port 6380
    return mini_redis::start_server(6379);
}
