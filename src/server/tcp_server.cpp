#include "server/tcp_server.hpp"
#include "utils/logger.hpp"

#include "../protocol/parser.hpp"
#include "../storage/kv_store.hpp"

#include <string>
#include <thread>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#error "This implementation currently supports only Windows (Winsock)."
#endif

namespace mini_redis {

namespace {

// RESP helpers
std::string resp_simple(const std::string &msg) {
    return "+" + msg + "\r\n";
}

std::string resp_bulk(const std::string &msg) {
    return "$" + std::to_string(msg.size()) + "\r\n" + msg + "\r\n";
}

std::string resp_nil() {
    return "$-1\r\n";
}

std::string resp_err(const std::string &msg) {
    return "-" + msg + "\r\n";
}

// Shared key-value store for all clients
KVStore kv;

void handle_client(SOCKET client_socket) {
    Logger::log(Logger::Level::Info, "Client connected");

    char buffer[1024];

    while (true) {
        int bytes = recv(client_socket, buffer, static_cast<int>(sizeof(buffer) - 1), 0);
        if (bytes <= 0) {
            break;
        }

        buffer[bytes] = '\0';
        std::string input(buffer);

        Logger::log(Logger::Level::Info, "Received: " + input);

        protocol::Command cmd = protocol::parse_command(input);
        std::string reply;

        switch (cmd.type) {
            case protocol::CommandType::PING:
                reply = resp_simple("PONG");
                break;

            case protocol::CommandType::ECHO:
                if (cmd.args.empty())
                    reply = resp_err("ECHO requires a message");
                else
                    reply = resp_bulk(cmd.args[0]);
                break;

            case protocol::CommandType::SET:
                if (cmd.args.size() < 2)
                    reply = resp_err("SET requires key and value");
                else {
                    kv.set(cmd.args[0], cmd.args[1]);
                    reply = resp_simple("OK");
                }
                break;

            case protocol::CommandType::GET:
                if (cmd.args.empty())
                    reply = resp_err("GET requires a key");
                else {
                    std::string value;
                    if (kv.get(cmd.args[0], value))
                        reply = resp_bulk(value);
                    else
                        reply = resp_nil();
                }
                break;

            default:
                reply = resp_err("Unknown command");
                break;
        }

        send(client_socket, reply.c_str(), static_cast<int>(reply.size()), 0);
    }

    Logger::log(Logger::Level::Info, "Client disconnected");
    closesocket(client_socket);
}

bool init_winsock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        Logger::log(Logger::Level::Error, "WSAStartup failed with error " + std::to_string(result));
        return false;
    }
    return true;
}

} // anonymous namespace

int start_server(int port) {
    if (!init_winsock()) {
        return 1;
    }

    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET) {
        Logger::log(Logger::Level::Error, "socket() failed");
        WSACleanup();
        return 1;
    }

    char yes = 1;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        Logger::log(Logger::Level::Error, "bind() failed");
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        Logger::log(Logger::Level::Error, "listen() failed");
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    Logger::log(Logger::Level::Info, "Mini-Redis running on port " + std::to_string(port));

    while (true) {
        SOCKET client_socket = accept(listen_socket, nullptr, nullptr);
        if (client_socket == INVALID_SOCKET) {
            Logger::log(Logger::Level::Error, "accept() failed");
            continue;
        }

        std::thread t(handle_client, client_socket);
        t.detach();
    }

    closesocket(listen_socket);
    WSACleanup();
    return 0;
}

} // namespace mini_redis
