#include <iostream>
#include <thread>
#include <vector>
#include <sstream>
#include <string>
#include <winsock2.h>
#include "../storage/kv_store.cpp"
#pragma comment(lib, "ws2_32.lib")

KeyValueStore kv; // global instance

std::vector<std::string> split(const std::string& s) {
    std::istringstream iss(s);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) tokens.push_back(token);
    return tokens;
}

void handle_client(SOCKET client_socket) {
    char buffer[1024];
    while (true) {
        int bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;

        buffer[bytes] = '\0';
        std::string input(buffer);
        auto parts = split(input);

        std::string response;
        if (parts.size() >= 3 && parts[0] == "SET") {
            response = kv.set(parts[1], parts[2]);
        } else if (parts.size() >= 2 && parts[0] == "GET") {
            response = kv.get(parts[1]);
        } else {
            response = "-ERR unknown command\r\n";
        }

        send(client_socket, response.c_str(), response.size(), 0);
    }

    closesocket(client_socket);
}

int start_server(int port) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_socket, SOMAXCONN);

    std::cout << "Listening on port " << port << "..." << std::endl;

    while (true) {
        SOCKET client_socket = accept(server_socket, nullptr, nullptr);
        std::thread(handle_client, client_socket).detach();
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}
