// AOF (Append-Only File) logger for Mini-Redis
// Logs all write commands to a file in RESP format using a background thread

#pragma once

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <fstream>

namespace protocol {
    struct Command;
}

class KVStore;

class AOFLogger {
public:
    AOFLogger(const std::string& filename);
    ~AOFLogger();
    
    // Append a command to the AOF log (non-blocking)
    void append(const protocol::Command& cmd);
    
    // Replay AOF file to restore state in a KVStore
    bool replay(KVStore& store);
    
    // Start the background writer thread
    void start();
    
    // Stop the background writer thread gracefully
    void stop();

private:
    // Background thread function that writes commands to file
    void writer_thread_func();
    
    std::string filename_;
    std::ofstream aof_file_;
    std::queue<std::string> command_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread writer_thread_;
    std::atomic<bool> running_;
};

