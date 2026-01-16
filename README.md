# Mini-Redis

A lightweight, Redis-inspired in-memory key-value store written in C++ for Windows.

## Features

- **TCP Server**: Thread-per-client or IOCP (I/O Completion Ports) modes
- **RESP Protocol**: Full Redis Serialization Protocol with pipelining support
- **Thread-Safe Store**: Key-value operations with expiration and LRU eviction
- **Persistence**: RDB snapshots and AOF logging
- **Replication**: Basic primary-replica support
- **Benchmarking**: Python and C++ benchmark tools

## Supported Commands

| Command | Description |
|---------|-------------|
| PING | Test server connection |
| ECHO msg | Return message |
| SET key value | Set key to value |
| GET key | Get value of key |
| DEL key | Delete key |
| EXISTS key | Check if key exists |
| KEYS pattern | List all keys |
| EXPIRE key secs | Set expiration |
| TTL key | Get time-to-live |
| MGET key1 key2... | Get multiple keys |
| INCR key | Increment integer value |
| DECR key | Decrement integer value |
| INCRBY key n | Increment by n |
| DECRBY key n | Decrement by n |
| APPEND key value | Append to string |
| STRLEN key | Get string length |
| SAVE | Save to RDB file |
| LOAD | Load from RDB file |
| INFO | Server information |
| QUIT | Close connection |

## Building

### Prerequisites

- Windows 10/11
- CMake 3.10+
- C++17 compiler (VS 2019+ or MinGW-w64)

### Quick Build

From Developer Command Prompt:

```powershell
mkdir build && cd build
cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
nmake
```

Or use the build script:

```powershell
.\build.ps1 -Force
```

## Running

### Server Options

```powershell
.\mini_redis.exe [options]

Options:
  -p, --port PORT      Server port (default: 6379)
  -m, --max-keys N     Max keys before LRU eviction (default: 10000)
  -a, --aof PATH       AOF file path
  -r, --rdb PATH       RDB file path
  -c, --config PATH    Load config file
      --iocp           Use IOCP server (high performance)
  -h, --help           Show help
```

### Config File Format

```ini
# mini_redis.conf
port = 6379
max_keys = 10000
use_iocp = true
aof_path = mini_redis.aof
rdb_path = mini_redis_dump.rdb
```

### Example Session

```
> PING
+PONG

> SET user alice
+OK

> GET user
$5
alice

> INCR counter
:1

> INCRBY counter 10
:11

> APPEND msg "Hello"
:5

> STRLEN msg
:5
```

## Benchmarking

### Python Benchmark

```powershell
python mini_redis_benchmark.py [options]

Options:
  --host HOST           Server host (default: localhost)
  --port PORT           Server port (default: 6379)
  --single-ops N        Single-client operations (default: 50000)
  --multi-clients N     Concurrent clients (default: 5)
  --multi-ops N         Ops per client (default: 20000)
  --correctness-only    Run only correctness tests
  --compare-modes       Help compare thread vs IOCP modes
```

### C++ Load Generator

```powershell
.\loadgen.exe --host localhost --port 6379 --requests 10000 --threads 4
```

## Testing

```powershell
# Build and run tests
cd build
nmake mini_redis_tests
.\mini_redis_tests.exe
```

Tests include:
- Parser tests
- RESP parser tests
- KVStore tests
- Atomic command tests (INCR/DECR/INCRBY/DECRBY/APPEND/STRLEN)
- Configuration tests

## Project Structure

```
mini-redis/
├── src/
│   ├── main.cpp                  # Entry point with CLI
│   ├── server/
│   │   ├── tcp_server.cpp/hpp    # Thread-per-client server
│   │   ├── iocp_server.cpp       # IOCP async server
│   │   └── replication.cpp/hpp   # Replication manager
│   ├── storage/
│   │   ├── kv_store.cpp/hpp      # Key-value store
│   │   └── aof_logger.cpp/hpp    # AOF logging
│   ├── protocol/
│   │   ├── parser.cpp/hpp        # Command parser
│   │   ├── resp_parser.cpp/hpp   # RESP protocol parser
│   │   └── resp_utils.cpp/hpp    # RESP serialization helpers
│   └── utils/
│       ├── config.cpp/hpp        # Configuration parsing
│       └── logger.hpp            # Thread-safe logging
├── tests/
│   ├── test_protocol.cpp         # Main test runner
│   ├── test_atomic_commands.cpp  # Atomic op tests
│   └── test_config.cpp           # Config tests
├── bench/
│   └── loadgen.cpp               # C++ load generator
├── CMakeLists.txt
├── mini_redis_benchmark.py       # Python benchmark
└── build.ps1                     # Build script
```

## Implementation Notes

### Thread Safety
- KVStore: mutex-protected operations
- Atomic counters for server statistics
- AOF: background writer thread with queue
- Replication: mutex-protected replica list

### Memory Management
- LRU eviction at 10,000 keys (configurable)
- Lazy expiration on key access
- Binary RDB format for persistence

### Server Modes
- **Thread-per-client**: Simple, one thread per connection
- **IOCP**: Windows async I/O, better for high concurrency

## License

See LICENSE file.

## Author

Sritej Bommaraju  
EECS + Applied Math, UC Berkeley
