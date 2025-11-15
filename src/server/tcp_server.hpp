#pragma once

namespace mini_redis {

// Starts the TCP server on the given port (e.g. 6379).
// Blocks the calling thread and runs the accept loop.
// Returns 0 on normal shutdown, non-zero on fatal error.
int start_server(int port);

} // namespace mini_redis
