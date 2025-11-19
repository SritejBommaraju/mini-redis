// IOCP-based server implementation for Mini-Redis
// Uses AcceptEx and asynchronous I/O with completion ports

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mswsock.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")
#else
#error "This implementation currently supports only Windows (Winsock)."
#endif

#include "server/tcp_server.hpp"
#include "utils/logger.hpp"

#include "../protocol/parser.hpp"
#include "../protocol/resp_parser.hpp"
#include "../storage/kv_store.hpp"
#include "../storage/aof_logger.hpp"
#include "replication.hpp"

#include <string>
#include <thread>
#include <vector>
#include <map>
#include <set>
#include <ctime>
#include <sstream>
#include <mutex>
#include <atomic>
#include <fstream>

#include "server/server_common.hpp"

namespace mini_redis {

// Forward declarations for shared functions from tcp_server.cpp
std::vector<std::vector<std::string>> extract_resp_commands(RespParser* parser, std::string* error_msg = nullptr);
mini_redis::detail::CommandResult process_command(const protocol::Command& cmd, mini_redis::detail::ClientContext& ctx, SOCKET client_socket);
KVStore& get_db(mini_redis::detail::ClientContext& ctx);

namespace {

// RESP helpers (same as tcp_server.cpp)
std::string resp_simple(const std::string &msg) {
    return "+" + msg + "\r\n";
}

std::string resp_bulk(const std::string &msg) {
    return "$" + std::to_string(msg.size()) + "\r\n" + msg + "\r\n";
}

std::string resp_nil() {
    return "$-1\r\n";
}

std::string resp_integer(int value) {
    return ":" + std::to_string(value) + "\r\n";
}

std::string resp_array(const std::vector<std::string>& items) {
    std::string result = "*" + std::to_string(items.size()) + "\r\n";
    for (const auto& item : items) {
        result += "$" + std::to_string(item.size()) + "\r\n" + item + "\r\n";
    }
    return result;
}

std::string resp_err(const std::string &msg) {
    return "-" + msg + "\r\n";
}

// IOCP client context: extends ClientContext with async I/O structures
struct IOCPClientContext {
    mini_redis::detail::ClientContext ctx;  // Reuse existing context
    OVERLAPPED overlapped;       // For async operations
    WSABUF wsa_buf;              // Buffer descriptor
    char read_buffer[4096];      // Read buffer
    std::string write_buffer;    // Accumulated write data
    std::string pending_write;   // Data being written (kept alive during async op)
    SOCKET socket;               // Client socket
    enum Operation { OP_READ, OP_WRITE, OP_ACCEPT } operation;
    
    IOCPClientContext() : socket(INVALID_SOCKET), operation(OP_READ) {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
        wsa_buf.buf = read_buffer;
        wsa_buf.len = sizeof(read_buffer);
    }
};

// Accept context for AcceptEx operations
struct AcceptContext {
    OVERLAPPED overlapped;
    SOCKET accept_socket;
    char buffer[2 * (sizeof(sockaddr_in) + 16)]; // Required for AcceptEx
    
    AcceptContext() : accept_socket(INVALID_SOCKET) {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
    }
};

HANDLE g_completion_port = INVALID_HANDLE_VALUE;
SOCKET g_listen_socket = INVALID_SOCKET;
LPFN_ACCEPTEX g_AcceptEx = nullptr;
std::atomic<bool> g_running{true};
const int WORKER_THREAD_COUNT = 6;

// Load AcceptEx function pointer
bool load_acceptex(SOCKET listen_socket) {
    GUID guidAcceptEx = WSAID_ACCEPTEX;
    DWORD bytes;
    int result = WSAIoctl(
        listen_socket,
        SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guidAcceptEx,
        sizeof(guidAcceptEx),
        &g_AcceptEx,
        sizeof(g_AcceptEx),
        &bytes,
        nullptr,
        nullptr
    );
    return result == 0;
}

// Post a new AcceptEx operation
void post_accept(SOCKET listen_socket) {
    AcceptContext* accept_ctx = new AcceptContext();
    accept_ctx->accept_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if (accept_ctx->accept_socket == INVALID_SOCKET) {
        delete accept_ctx;
        return;
    }
    
    DWORD bytes = 0;
    BOOL result = g_AcceptEx(
        listen_socket,
        accept_ctx->accept_socket,
        accept_ctx->buffer,
        0,
        sizeof(sockaddr_in) + 16,
        sizeof(sockaddr_in) + 16,
        &bytes,
        &accept_ctx->overlapped
    );
    
    if (!result && WSAGetLastError() != WSA_IO_PENDING) {
        closesocket(accept_ctx->accept_socket);
        delete accept_ctx;
    }
}

// Post a read operation for a client
void post_read(IOCPClientContext* client_ctx) {
    client_ctx->operation = IOCPClientContext::OP_READ;
    client_ctx->wsa_buf.buf = client_ctx->read_buffer;
    client_ctx->wsa_buf.len = sizeof(client_ctx->read_buffer);
    ZeroMemory(&client_ctx->overlapped, sizeof(OVERLAPPED));
    
    DWORD flags = 0;
    int result = WSARecv(
        client_ctx->socket,
        &client_ctx->wsa_buf,
        1,
        nullptr,
        &flags,
        &client_ctx->overlapped,
        nullptr
    );
    
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        // Error - close connection
        closesocket(client_ctx->socket);
        delete client_ctx;
    }
}

// Post a write operation for a client
void post_write(IOCPClientContext* client_ctx) {
    if (client_ctx->write_buffer.empty()) {
        // No data to write, post next read
        post_read(client_ctx);
        return;
    }
    
    client_ctx->operation = IOCPClientContext::OP_WRITE;
    
    // Move write buffer to pending_write to keep it alive during async operation
    client_ctx->pending_write = std::move(client_ctx->write_buffer);
    client_ctx->write_buffer.clear();
    
    WSABUF wsa_buf;
    wsa_buf.buf = const_cast<char*>(client_ctx->pending_write.c_str());
    wsa_buf.len = static_cast<ULONG>(client_ctx->pending_write.size());
    ZeroMemory(&client_ctx->overlapped, sizeof(OVERLAPPED));
    
    DWORD bytes_sent = 0;
    int result = WSASend(
        client_ctx->socket,
        &wsa_buf,
        1,
        &bytes_sent,
        0,
        &client_ctx->overlapped,
        nullptr
    );
    
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        // Error - close connection
        closesocket(client_ctx->socket);
        delete client_ctx;
    }
}

// Worker thread function
DWORD WINAPI worker_thread(LPVOID param) {
    (void)param; // Unused parameter - required by WINAPI signature
    while (g_running) {
        DWORD bytes_transferred = 0;
        ULONG_PTR completion_key = 0;
        OVERLAPPED* overlapped = nullptr;
        
        BOOL success = GetQueuedCompletionStatus(
            g_completion_port,
            &bytes_transferred,
            &completion_key,
            &overlapped,
            INFINITE
        );
        
        if (!success) {
            if (overlapped == nullptr) {
                // Shutdown signal
                break;
            }
            // Error - cleanup
            if (completion_key == 0) {
                // Accept context
                AcceptContext* accept_ctx = CONTAINING_RECORD(overlapped, AcceptContext, overlapped);
                closesocket(accept_ctx->accept_socket);
                delete accept_ctx;
                // Post new accept
                if (g_running) {
                    post_accept(g_listen_socket);
                }
            } else {
                // Client context
                IOCPClientContext* client_ctx = reinterpret_cast<IOCPClientContext*>(completion_key);
                closesocket(client_ctx->socket);
                // Cleanup from channels
                {
                    std::lock_guard<std::mutex> lock(mini_redis::detail::channels_mutex);
                    for (auto& pair : mini_redis::detail::channels) {
                        pair.second.erase(client_ctx->socket);
                    }
                }
                delete client_ctx;
            }
            continue;
        }
        
        if (completion_key == 0) {
            // Accept completion
            AcceptContext* accept_ctx = CONTAINING_RECORD(overlapped, AcceptContext, overlapped);
            
            // Update accept context
            setsockopt(
                accept_ctx->accept_socket,
                SOL_SOCKET,
                SO_UPDATE_ACCEPT_CONTEXT,
                reinterpret_cast<char*>(&g_listen_socket),
                sizeof(g_listen_socket)
            );
            
            // Create client context
            IOCPClientContext* client_ctx = new IOCPClientContext();
            client_ctx->socket = accept_ctx->accept_socket;
            
            // Associate accepted socket with completion port (using client context as key)
            CreateIoCompletionPort(
                reinterpret_cast<HANDLE>(client_ctx->socket),
                g_completion_port,
                reinterpret_cast<ULONG_PTR>(client_ctx),
                0
            );
            
            mini_redis::Logger::log(mini_redis::Logger::Level::Info, "Client connected (IOCP)");
            post_read(client_ctx);
            
            // Post new accept (cleanup accept context, socket is now owned by client_ctx)
            delete accept_ctx;
            if (g_running) {
                post_accept(g_listen_socket);
            }
        } else {
            // Client I/O completion
            IOCPClientContext* client_ctx = reinterpret_cast<IOCPClientContext*>(completion_key);
            
            if (client_ctx->operation == IOCPClientContext::OP_READ) {
                if (bytes_transferred == 0) {
                    // Client disconnected
                    closesocket(client_ctx->socket);
                    {
                        std::lock_guard<std::mutex> lock(mini_redis::detail::channels_mutex);
                        for (auto& pair : mini_redis::detail::channels) {
                            pair.second.erase(client_ctx->socket);
                        }
                    }
                    mini_redis::Logger::log(mini_redis::Logger::Level::Info, "Client disconnected (IOCP)");
                    delete client_ctx;
                    continue;
                }
                
                // Append received data to parser
                // Note: client_ctx->read_buffer is the raw socket buffer (char[4096])
                // Parser maintains its own buffer and handles binary data correctly
                if (client_ctx->ctx.parser) {
                    client_ctx->ctx.parser->append(client_ctx->read_buffer, bytes_transferred);
                }
                
                // Extract and process RESP commands
                std::string parse_error;
                std::vector<std::vector<std::string>> resp_commands = extract_resp_commands(client_ctx->ctx.parser, &parse_error);
                
                // If we got a parse error and no commands, send error and log details
                if (resp_commands.empty() && !parse_error.empty()) {
                    // Log the parse error with buffer context for debugging
                    mini_redis::Logger::log(mini_redis::Logger::Level::Warn, "RESP parse error (IOCP): " + parse_error);
                    std::string error_reply = resp_err(parse_error);
                    client_ctx->write_buffer += error_reply;
                    // Post write if we have data, otherwise post next read
                    // Don't clear buffer - allows connection to recover if next command is valid
                    if (!client_ctx->write_buffer.empty()) {
                        post_write(client_ctx);
                    } else {
                        post_read(client_ctx);
                    }
                    goto next_operation;
                }
                
                // Process each command
                for (const auto& resp_array : resp_commands) {
                    // Convert RESP array to Command struct
                    protocol::Command cmd = protocol::command_from_resp_array(resp_array);
                    
                    // Handle parse errors
                    if (cmd.type == protocol::CommandType::UNKNOWN && !resp_array.empty()) {
                        std::string error_reply = resp_err("ERR unknown command '" + resp_array[0] + "'");
                        client_ctx->write_buffer += error_reply;
                        continue;
                    }
                    
                    mini_redis::detail::CommandResult result = process_command(cmd, client_ctx->ctx, client_ctx->socket);
                    
                    // Accumulate reply
                    client_ctx->write_buffer += result.reply;
                    
                    if (result.should_quit) {
                        // Client wants to quit
                        closesocket(client_ctx->socket);
                        {
                            std::lock_guard<std::mutex> lock(mini_redis::detail::channels_mutex);
                            for (auto& pair : mini_redis::detail::channels) {
                                pair.second.erase(client_ctx->socket);
                            }
                        }
                        delete client_ctx;
                        goto next_operation;
                    }
                }
                
                // Post write if we have data, otherwise post next read
                if (!client_ctx->write_buffer.empty()) {
                    post_write(client_ctx);
                } else {
                    post_read(client_ctx);
                }
            } else if (client_ctx->operation == IOCPClientContext::OP_WRITE) {
                // Write completed, clear pending write and post next read
                client_ctx->pending_write.clear();
                post_read(client_ctx);
            }
        }
        
        next_operation:;
    }
    return 0;
}

bool init_winsock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        mini_redis::Logger::log(mini_redis::Logger::Level::Error, "WSAStartup failed with error " + std::to_string(result));
        return false;
    }
    return true;
}

} // anonymous namespace

int start_server_iocp(int port) {
    if (!init_winsock()) {
        return 1;
    }
    
    // Initialize AOF logger (shared with tcp_server)
    // Note: This creates a separate instance; in production, these should share the same instance
    static AOFLogger aof_logger("mini_redis.aof");
    g_aof_logger = &aof_logger;
    g_aof_logger->start();
    
    // Replay AOF file on startup if it exists
    std::ifstream aof_check("mini_redis.aof");
    if (aof_check.good()) {
        aof_check.close();
        if (g_aof_logger->replay(mini_redis::detail::databases[0])) {
            mini_redis::Logger::log(mini_redis::Logger::Level::Info, "AOF file replayed successfully");
        }
    }
    
    // Initialize replication manager
    static ReplicationManager replication_manager;
    g_replication_manager = &replication_manager;
    g_replication_manager->start();
    
    // Create completion port
    g_completion_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (g_completion_port == nullptr) {
        mini_redis::Logger::log(mini_redis::Logger::Level::Error, "CreateIoCompletionPort failed");
        WSACleanup();
        return 1;
    }
    
    // Create listen socket
    g_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen_socket == INVALID_SOCKET) {
        mini_redis::Logger::log(mini_redis::Logger::Level::Error, "socket() failed");
        CloseHandle(g_completion_port);
        WSACleanup();
        return 1;
    }
    
    // Associate listen socket with completion port
    CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_listen_socket), g_completion_port, 0, 0);
    
    char yes = 1;
    setsockopt(g_listen_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(g_listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        mini_redis::Logger::log(mini_redis::Logger::Level::Error, "bind() failed");
        closesocket(g_listen_socket);
        CloseHandle(g_completion_port);
        WSACleanup();
        return 1;
    }
    
    if (listen(g_listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        mini_redis::Logger::log(mini_redis::Logger::Level::Error, "listen() failed");
        closesocket(g_listen_socket);
        CloseHandle(g_completion_port);
        WSACleanup();
        return 1;
    }
    
    // Load AcceptEx
    if (!load_acceptex(g_listen_socket)) {
        mini_redis::Logger::log(mini_redis::Logger::Level::Error, "Failed to load AcceptEx");
        closesocket(g_listen_socket);
        CloseHandle(g_completion_port);
        WSACleanup();
        return 1;
    }
    
    mini_redis::Logger::log(mini_redis::Logger::Level::Info, "Mini-Redis IOCP server running on port " + std::to_string(port));
    
    // Start worker threads
    std::vector<HANDLE> worker_threads;
    for (int i = 0; i < WORKER_THREAD_COUNT; ++i) {
        HANDLE thread = CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr);
        if (thread != nullptr) {
            worker_threads.push_back(thread);
        }
    }
    
    // Post initial AcceptEx operations
    for (int i = 0; i < 10; ++i) {
        post_accept(g_listen_socket);
    }
    
    // Main loop - just wait (worker threads handle everything)
    while (g_running) {
        Sleep(1000);
    }
    
    // Shutdown
    g_running = false;
    
    // Signal all worker threads to exit
    for (int i = 0; i < WORKER_THREAD_COUNT; ++i) {
        PostQueuedCompletionStatus(g_completion_port, 0, 0, nullptr);
    }
    
    // Wait for threads
    WaitForMultipleObjects(static_cast<DWORD>(worker_threads.size()), worker_threads.data(), TRUE, INFINITE);
    for (HANDLE thread : worker_threads) {
        CloseHandle(thread);
    }
    
    // Cleanup AOF logger
    if (g_aof_logger) {
        g_aof_logger->stop();
    }
    
    // Cleanup replication manager
    if (g_replication_manager) {
        g_replication_manager->stop();
    }
    
    closesocket(g_listen_socket);
    CloseHandle(g_completion_port);
    WSACleanup();
    return 0;
}

} // namespace mini_redis

