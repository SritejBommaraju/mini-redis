# Mini-Redis (Distributed In-Memory Key-Value Store)

A lightweight, Redis-inspired in-memory key–value store written in C++, featuring a TCP server, request parser, thread-safe storage engine, and optional persistence hooks. Designed as a systems-level project to demonstrate networking, concurrency, CMake, and low-level performance engineering.

## Features
- TCP Server: handles multiple clients over TCP, designed for async or multithreading expansion.
- In-Memory Store: thread-safe GET/SET/DEL operations using std::unordered_map + mutex.
- Protocol Parser: simple Redis-like text protocol.
- Thread-Safe Logging: info/warn/error logging with connection and command logs.
- CMake Build System: cross-platform modular build.

## Project Structure
mini-redis/
├── src/
│   ├── main.cpp
│   ├── server/
│   │   └── tcp_server.cpp
│   ├── storage/
│   │   └── kv_store.cpp
│   ├── protocol/
│   │   └── parser.cpp
│   └── utils/
│       └── logger.hpp
├── CMakeLists.txt
└── README.md

## Running the Server
cd build
cmake --build .
./mini_redis       # Windows: .\mini_redis.exe

Expected output:
[INFO] Mini-Redis server starting...
[INFO] Listening on port 6379...


## Connecting to Mini-Redis
Using netcat:
nc localhost 6379

Example commands:

SET user sritej
GET user
DEL user

Server logs:

[INFO] Client connected
[INFO] SET user sritej
[INFO] GET user
[INFO] DEL user
[INFO] Client disconnected


## Build Instructions (CMake)

mkdir build
cd build
cmake -G "NMake Makefiles" ..
cmake --build .


## Future Improvements
- Async IO (epoll or IOCP)
- Multi-threaded worker pool
- Replication and sharding
- Write-Ahead Logging and snapshot persistence
- Additional data structures (lists, sets, sorted sets)

## Why This Project Matters
This project demonstrates:
- TCP networking fundamentals
- Low-level C++ and memory management
- Thread safety and synchronization
- Protocol parsing and command execution
- Clean modular architecture similar to Redis
- Strong CMake and build system skills

## Author
Sritej Bommaraju  
EECS + Applied Math, UC Berkeley  
Systems engineering, distributed systems, and AI infrastructure.
