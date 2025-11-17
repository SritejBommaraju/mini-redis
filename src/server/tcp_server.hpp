// TCP server interface for Mini-Redis
// Provides the main server entry point for accepting client connections

#pragma once

namespace mini_redis {

// Starts the TCP server on the given port (e.g. 6379).
// Blocks the calling thread and runs the accept loop.
// Returns 0 on normal shutdown, non-zero on fatal error.
int start_server(int port);

// Starts the IOCP-based server on the given port.
// Uses asynchronous I/O with completion ports and worker thread pool.
// Returns 0 on normal shutdown, non-zero on fatal error.
int start_server_iocp(int port);

} // namespace mini_redis
