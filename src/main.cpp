#include "server/tcp_server.hpp"

int main() {
    // For now we always listen on 6379.
    // Later we can add CLI args like: mini_redis --port 6380
    return mini_redis::start_server(6379);
}
