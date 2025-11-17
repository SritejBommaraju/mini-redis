// AOF (Append-Only File) logger implementation
// Logs write commands to file in RESP format using background thread

#include "aof_logger.hpp"
#include "../protocol/parser.hpp"
#include "kv_store.hpp"
#include "../utils/logger.hpp"

#include <sstream>
#include <algorithm>
#include <cctype>

AOFLogger::AOFLogger(const std::string& filename) 
    : filename_(filename), running_(false) {
    aof_file_.open(filename, std::ios::app);
    if (!aof_file_.is_open()) {
        mini_redis::Logger::log(mini_redis::Logger::Level::Warn, "Failed to open AOF file: " + filename);
    }
}

AOFLogger::~AOFLogger() {
    stop();
}

void AOFLogger::start() {
    if (running_) {
        return;
    }
    running_ = true;
    writer_thread_ = std::thread(&AOFLogger::writer_thread_func, this);
}

void AOFLogger::stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    queue_cv_.notify_all();
    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }
    if (aof_file_.is_open()) {
        aof_file_.close();
    }
}

// Convert command to RESP array format: *<count>\r\n$<len>\r\n<arg>\r\n...
static std::string command_to_resp(const protocol::Command& cmd) {
    std::string result;
    
    // Determine command name from type
    std::string cmd_name;
    switch (cmd.type) {
        case protocol::CommandType::SET: cmd_name = "SET"; break;
        case protocol::CommandType::DEL: cmd_name = "DEL"; break;
        case protocol::CommandType::EXPIRE: cmd_name = "EXPIRE"; break;
        default: return ""; // Only log write commands
    }
    
    // Count total arguments (command name + args)
    size_t arg_count = 1 + cmd.args.size();
    
    // Build RESP array: *<count>\r\n
    result = "*" + std::to_string(arg_count) + "\r\n";
    
    // Add command name as bulk string: $<len>\r\n<value>\r\n
    result += "$" + std::to_string(cmd_name.size()) + "\r\n" + cmd_name + "\r\n";
    
    // Add each argument as bulk string
    for (const auto& arg : cmd.args) {
        result += "$" + std::to_string(arg.size()) + "\r\n" + arg + "\r\n";
    }
    
    return result;
}

void AOFLogger::append(const protocol::Command& cmd) {
    // Only log write commands
    if (cmd.type != protocol::CommandType::SET && 
        cmd.type != protocol::CommandType::DEL && 
        cmd.type != protocol::CommandType::EXPIRE) {
        return;
    }
    
    std::string resp_cmd = command_to_resp(cmd);
    if (resp_cmd.empty()) {
        return;
    }
    
    // Add to queue (non-blocking)
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        command_queue_.push(resp_cmd);
    }
    queue_cv_.notify_one();
}

void AOFLogger::writer_thread_func() {
    while (running_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        // Wait for commands or shutdown signal
        queue_cv_.wait(lock, [this] { return !command_queue_.empty() || !running_; });
        
        // Process all queued commands
        while (!command_queue_.empty()) {
            std::string cmd = command_queue_.front();
            command_queue_.pop();
            lock.unlock();
            
            // Write to file
            if (aof_file_.is_open()) {
                aof_file_ << cmd;
                aof_file_.flush(); // Ensure data is written
            }
            
            lock.lock();
        }
    }
}

// Simple RESP array parser for AOF replay
// Parses format: *<count>\r\n$<len>\r\n<value>\r\n...
static bool parse_resp_array(const std::string& resp_data, size_t& pos, std::vector<std::string>& args) {
    if (pos >= resp_data.length() || resp_data[pos] != '*') {
        return false;
    }
    pos++; // Skip '*'
    
    // Read count
    size_t count_end = resp_data.find("\r\n", pos);
    if (count_end == std::string::npos) {
        return false;
    }
    int count = std::stoi(resp_data.substr(pos, count_end - pos));
    pos = count_end + 2; // Skip \r\n
    
    // Read each bulk string
    for (int i = 0; i < count; ++i) {
        if (pos >= resp_data.length() || resp_data[pos] != '$') {
            return false;
        }
        pos++; // Skip '$'
        
        // Read length
        size_t len_end = resp_data.find("\r\n", pos);
        if (len_end == std::string::npos) {
            return false;
        }
        int len = std::stoi(resp_data.substr(pos, len_end - pos));
        pos = len_end + 2; // Skip \r\n
        
        // Read value
        if (pos + len > resp_data.length()) {
            return false;
        }
        args.push_back(resp_data.substr(pos, len));
        pos += len;
        
        // Skip \r\n after value
        if (pos + 2 > resp_data.length() || resp_data.substr(pos, 2) != "\r\n") {
            return false;
        }
        pos += 2;
    }
    
    return true;
}

bool AOFLogger::replay(KVStore& store) {
    // Close write file if open
    if (aof_file_.is_open()) {
        aof_file_.close();
    }
    
    std::ifstream file(filename_);
    if (!file.is_open()) {
        return false;
    }
    
    // Read entire file
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    if (content.empty()) {
        return true; // Empty file is valid
    }
    
    // Parse RESP arrays from content
    size_t pos = 0;
    while (pos < content.length()) {
        std::vector<std::string> args;
        size_t start_pos = pos;
        
        if (!parse_resp_array(content, pos, args)) {
            // Skip to next potential command or end
            size_t next_star = content.find('*', start_pos + 1);
            if (next_star == std::string::npos) {
                break;
            }
            pos = next_star;
            continue;
        }
        
        if (args.empty()) {
            continue;
        }
        
        // Build command from args (first arg is command name)
        protocol::Command cmd;
        std::string cmd_name = args[0];
        for (char& c : cmd_name) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        
        if (cmd_name == "SET") cmd.type = protocol::CommandType::SET;
        else if (cmd_name == "DEL") cmd.type = protocol::CommandType::DEL;
        else if (cmd_name == "EXPIRE") cmd.type = protocol::CommandType::EXPIRE;
        else continue; // Skip unknown commands
        
        // Copy remaining args
        if (args.size() > 1) {
            cmd.args.assign(args.begin() + 1, args.end());
        }
        
        // Execute command on store
        if (cmd.type == protocol::CommandType::SET && cmd.args.size() >= 2) {
            store.set(cmd.args[0], cmd.args[1]);
        } else if (cmd.type == protocol::CommandType::DEL && !cmd.args.empty()) {
            store.del(cmd.args[0]);
        } else if (cmd.type == protocol::CommandType::EXPIRE && cmd.args.size() >= 2) {
            try {
                int seconds = std::stoi(cmd.args[1]);
                store.expire(cmd.args[0], seconds);
            } catch (...) {
                // Ignore invalid expiration
            }
        }
    }
    
    // Reopen file for appending
    aof_file_.open(filename_, std::ios::app);
    
    return true;
}

