# Mini-Redis

A lightweight, Redis-inspired in-memory key-value store written in C++ for Windows. Features a TCP server with both thread-per-client and IOCP (I/O Completion Ports) implementations, RESP protocol support, persistence (RDB snapshots and AOF logging), and basic replication.

## Features

### Core Functionality
- **TCP Server**: Thread-per-client model with optional IOCP-based asynchronous I/O
- **RESP Protocol**: Full Redis Serialization Protocol support with pipelining
- **In-Memory Store**: Thread-safe key-value operations with expiration and LRU eviction
- **Command Set**: PING, ECHO, SET, GET, DEL, EXISTS, KEYS, EXPIRE, TTL, MGET, QUIT, SAVE, LOAD, SELECT, INFO, SUBSCRIBE, PUBLISH, EVAL (stub), AUTH (stub)

### Persistence
- **RDB Snapshots**: Binary format for fast save/load operations
- **AOF Logging**: Append-only file logging with background writer thread for durability

### Replication
- **Primary-Replica**: Basic replication support for write commands (SET, DEL, EXPIRE)

### Performance
- **Load Generator**: Multi-threaded benchmark tool for performance testing
- **IOCP Support**: High-performance asynchronous I/O on Windows

## Project Structure

```
mini-redis/
├── src/
│   ├── main.cpp                 # Server entry point
│   ├── server/
│   │   ├── tcp_server.cpp      # Thread-per-client server
│   │   ├── tcp_server.hpp
│   │   ├── iocp_server.cpp     # IOCP-based async server
│   │   ├── replication.cpp     # Replication manager
│   │   ├── replication.hpp
│   │   └── server_common.hpp   # Shared server types
│   ├── storage/
│   │   ├── kv_store.cpp        # Key-value store with expiration/LRU
│   │   ├── kv_store.hpp
│   │   ├── aof_logger.cpp      # AOF logging implementation
│   │   └── aof_logger.hpp
│   ├── protocol/
│   │   ├── parser.cpp          # RESP protocol parser
│   │   └── parser.hpp
│   └── utils/
│       └── logger.hpp           # Thread-safe logging
├── bench/
│   └── loadgen.cpp             # Load generator/benchmark tool
├── tests/
│   └── test_protocol.cpp       # Unit tests
├── CMakeLists.txt
├── start_server.ps1            # PowerShell build/start script
└── README.md
```

## Building

### Prerequisites
- Windows 10/11
- CMake 3.10 or higher
- C++17 compiler (Visual Studio 2019+ or MinGW-w64)
- Winsock2 (included with Windows)

### Build Steps

1. **Create build directory:**
   ```powershell
   mkdir build
   cd build
   ```

2. **Configure with CMake:**
   ```powershell
   # For Visual Studio
   cmake .. -G "Visual Studio 17 2022" -A x64
   
   # Or for MinGW
   cmake .. -G "MinGW Makefiles"
   ```

3. **Build:**
   ```powershell
   cmake --build . --config Release
   ```

4. **Run server:**
   ```powershell
   .\Release\mini_redis.exe
   ```

Or use the provided PowerShell scripts:
```powershell
# Build script (automatically stops running processes)
.\build.ps1

# Start server script
.\start_server.ps1
```

### Troubleshooting Build Issues

#### Linker Error: "cannot open mini_redis.exe for writing" (LNK1168)

This error occurs when `mini_redis.exe` is currently running and the linker cannot overwrite it. Solutions:

**Option 1: Use the build script (recommended)**
```powershell
.\build.ps1
```
The script automatically detects and stops running processes before building.

**Option 2: Manual process management**
```powershell
# Check for running processes
Get-Process mini_redis -ErrorAction SilentlyContinue

# Stop the process
Stop-Process -Name mini_redis -Force

# Then retry the build
cd build
nmake
```

**Option 3: Force kill with build script**
```powershell
.\build.ps1 -Force
```

## Running the Server

The server listens on port 6379 by default. You can connect using any Redis client or `netcat`:

```powershell
# Using netcat (if available)
nc localhost 6379
```

### Example Commands

```
PING
+PONG

SET user alice
+OK

GET user
$5
alice

EXISTS user
:1

EXPIRE user 60
:1

TTL user
:58

KEYS *
*1
$4
user

DEL user
:1
```

## Load Generator

The included load generator can benchmark server performance:

```powershell
.\loadgen.exe --host localhost --port 6379 --requests 10000 --threads 4
```

Options:
- `--host <host>`: Server hostname (default: localhost)
- `--port <port>`: Server port (default: 6379)
- `--requests <count>`: Total requests to send (default: 1000)
- `--threads <count>`: Number of worker threads (default: 1)
- `--help`: Show usage information

## Persistence

### RDB Snapshots
Save the current database state:
```
SAVE
+OK
```

Load from RDB file:
```
LOAD
+OK
```

RDB files are saved as `mini_redis_dump.rdb` in binary format.

### AOF Logging
AOF (Append-Only File) logging is automatically enabled. All write commands (SET, DEL, EXPIRE) are logged to `mini_redis.aof` in RESP format. On server restart, the AOF file is automatically replayed to restore state.

## Replication

The replication manager supports basic primary-to-replica replication. Write commands are automatically sent to all connected replicas. (Replica connection management is currently manual - add replicas programmatically via the ReplicationManager API.)

## Server Modes

### Thread-Per-Client (Default)
```cpp
mini_redis::start_server(6379);
```

### IOCP (High Performance)
```cpp
mini_redis::start_server_iocp(6379);
```

The IOCP implementation uses Windows I/O Completion Ports for asynchronous I/O, providing better scalability for high-concurrency workloads.

## Testing

Run unit tests:
```powershell
.\mini_redis_tests.exe
```

## Implementation Details

### RESP Pipelining
The server supports RESP pipelining - multiple commands can be sent in a single TCP read operation. The server parses and processes all complete commands, handling partial commands across multiple reads.

### Thread Safety
- KVStore operations are protected by mutexes
- Server statistics use atomic counters
- AOF logging uses background thread with queue
- Replication uses mutex-protected replica list

### Memory Management
- LRU eviction when store exceeds 10,000 keys
- Automatic expiration cleanup on key access
- Efficient binary RDB format for persistence

## License

See LICENSE file for details.

## Author

Sritej Bommaraju  
EECS + Applied Math, UC Berkeley  
Systems engineering, distributed systems, and AI infrastructure.
