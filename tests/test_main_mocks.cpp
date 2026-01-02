// Mock implementation of globals for testing purposes
// This is compiled into the test binary to satisfy linker dependencies

#include "server/server_common.hpp"
#include "storage/kv_store.hpp"
#include "storage/aof_logger.hpp"
#include "server/replication.hpp"
#include <mutex>
#include <vector>
#include <map>
#include <set>
#include <atomic>

// Define globals needed by commands.cpp
namespace mini_redis {
    namespace detail {
        std::vector<KVStore> databases(16);
        std::mutex databases_mutex;
        std::map<std::string, std::set<SOCKET>> channels;
        std::mutex channels_mutex;
        time_t server_start_time = time(nullptr);
        std::atomic<long long> total_commands_processed{0};
    }

    // Pointers to managers (can be null for testing)
    AOFLogger* g_aof_logger = nullptr;
    ReplicationManager* g_replication_manager = nullptr;
}

// Mock AOFLogger if needed (though we passed nullptr)
// But linker needs the class definition methods if they are called
// We are linking against existing headers but missing the cpp implementations of managers
// So we need to stub the methods called by commands.cpp

void AOFLogger::append(const protocol::Command&) {}
void ReplicationManager::replicate_command(const protocol::Command&) {}

// We need ClientContext ctor/dtor because they are in commands.cpp/tcp_server.cpp
// but we aren't linking tcp_server.cpp. They are defined in server_common.hpp as struct
// but the implementation of ctor/dtor was in tcp_server.cpp.
// We need to move them to a common place or redefine them here.

#include "protocol/resp_parser.hpp"

namespace mini_redis {
namespace detail {

ClientContext::ClientContext() {
    parser = new RespParser();
}

ClientContext::~ClientContext() {
    if (parser) {
        delete parser;
        parser = nullptr;
    }
}

}
}

// Global mock send function
int send(SOCKET, const char*, int, int) {
    return 0; // Success
}
