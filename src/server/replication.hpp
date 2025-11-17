// Replication manager for Mini-Redis
// Handles primary-to-replica replication of write commands

#pragma once

#include <string>
#include <vector>
#include <mutex>

namespace protocol {
    struct Command;
}

class ReplicationManager {
public:
    ReplicationManager();
    ~ReplicationManager();
    
    // Add a replica endpoint (host:port)
    void add_replica(const std::string& host, int port);
    
    // Remove a replica endpoint
    void remove_replica(const std::string& host, int port);
    
    // Replicate a write command to all connected replicas
    void replicate_command(const protocol::Command& cmd);
    
    // Start replication manager (initialize if needed)
    void start();
    
    // Stop replication manager (close all connections)
    void stop();

private:
    struct ReplicaEndpoint {
        std::string host;
        int port;
        void* socket; // SOCKET on Windows
        bool connected;
    };
    
    std::vector<ReplicaEndpoint> replicas_;
    std::mutex replicas_mutex_;
};

