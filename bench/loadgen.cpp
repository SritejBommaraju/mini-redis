// Load generator for Mini-Redis
// Connects to server and sends commands for performance benchmarking

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#error "This implementation currently supports only Windows (Winsock)."
#endif

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <atomic>
#include <sstream>
#include <iomanip>

// RESP protocol helpers
static std::string resp_array(const std::vector<std::string>& items) {
    std::string result = "*" + std::to_string(items.size()) + "\r\n";
    for (const auto& item : items) {
        result += "$" + std::to_string(item.size()) + "\r\n" + item + "\r\n";
    }
    return result;
}

// Connect to server
static SOCKET connect_to_server(const std::string& host, int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    
    return sock;
}

// Send command and receive response
static bool send_command(SOCKET sock, const std::vector<std::string>& cmd_parts, std::string& response) {
    std::string cmd = resp_array(cmd_parts);
    
    int sent = send(sock, cmd.c_str(), static_cast<int>(cmd.size()), 0);
    if (sent != static_cast<int>(cmd.size())) {
        return false;
    }
    
    // Read response (simplified - just read first line)
    char buffer[1024];
    int received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
        return false;
    }
    
    buffer[received] = '\0';
    response = std::string(buffer, received);
    return true;
}

// Worker thread function
static void worker_thread(const std::string& host, int port, int requests_per_thread, 
                         std::atomic<long long>& total_requests, std::atomic<long long>& successful_requests,
                         std::atomic<long long>& total_latency_us) {
    SOCKET sock = connect_to_server(host, port);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Failed to connect to " << host << ":" << port << std::endl;
        return;
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 999999);
    
    for (int i = 0; i < requests_per_thread; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Generate random key and value
        int key_num = dis(gen);
        int val_num = dis(gen);
        std::string key = "key" + std::to_string(key_num);
        std::string value = "value" + std::to_string(val_num);
        
        // Send SET command
        std::vector<std::string> set_cmd = {"SET", key, value};
        std::string response;
        bool success = send_command(sock, set_cmd, response);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        total_requests++;
        if (success) {
            successful_requests++;
        }
        total_latency_us += latency;
        
        // Optionally send GET command
        if (i % 2 == 0) {
            start = std::chrono::high_resolution_clock::now();
            std::vector<std::string> get_cmd = {"GET", key};
            send_command(sock, get_cmd, response);
            end = std::chrono::high_resolution_clock::now();
            latency = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            total_requests++;
            if (success) {
                successful_requests++;
            }
            total_latency_us += latency;
        }
    }
    
    closesocket(sock);
}

// Parse command line arguments
static void parse_args(int argc, char* argv[], std::string& host, int& port, 
                      int& total_requests, int& num_threads) {
    host = "localhost";
    port = 6379;
    total_requests = 1000;
    num_threads = 1;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--requests" && i + 1 < argc) {
            total_requests = std::stoi(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            num_threads = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: loadgen [options]\n"
                      << "Options:\n"
                      << "  --host <host>      Server hostname (default: localhost)\n"
                      << "  --port <port>      Server port (default: 6379)\n"
                      << "  --requests <num>  Total requests to send (default: 1000)\n"
                      << "  --threads <num>   Number of worker threads (default: 1)\n"
                      << "  --help, -h        Show this help message\n";
            exit(0);
        }
    }
}

int main(int argc, char* argv[]) {
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }
    
    // Parse arguments
    std::string host;
    int port, total_requests, num_threads;
    parse_args(argc, argv, host, port, total_requests, num_threads);
    
    std::cout << "Load Generator for Mini-Redis\n"
              << "Connecting to " << host << ":" << port << "\n"
              << "Total requests: " << total_requests << "\n"
              << "Threads: " << num_threads << "\n"
              << "Starting benchmark...\n\n";
    
    // Statistics
    std::atomic<long long> total_requests_sent{0};
    std::atomic<long long> successful_requests{0};
    std::atomic<long long> total_latency_us{0};
    
    // Calculate requests per thread
    int requests_per_thread = total_requests / num_threads;
    int extra_requests = total_requests % num_threads;
    
    // Start worker threads
    auto start_time = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        int requests = requests_per_thread + (i < extra_requests ? 1 : 0);
        threads.emplace_back(worker_thread, host, port, requests,
                           std::ref(total_requests_sent),
                           std::ref(successful_requests),
                           std::ref(total_latency_us));
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    // Print results
    long long requests = total_requests_sent.load();
    long long successful = successful_requests.load();
    long long latency_sum = total_latency_us.load();
    
    double requests_per_sec = (requests * 1000.0) / duration;
    double avg_latency_ms = (latency_sum / 1000.0) / requests;
    double success_rate = (successful * 100.0) / requests;
    
    std::cout << "\n=== Benchmark Results ===\n"
              << "Total requests: " << requests << "\n"
              << "Successful: " << successful << " (" << std::fixed << std::setprecision(2) 
              << success_rate << "%)\n"
              << "Duration: " << duration << " ms\n"
              << "Requests/sec: " << std::fixed << std::setprecision(2) << requests_per_sec << "\n"
              << "Avg latency: " << std::fixed << std::setprecision(2) << avg_latency_ms << " ms\n";
    
    WSACleanup();
    return 0;
}

