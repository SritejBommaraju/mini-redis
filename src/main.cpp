#include <iostream>

int start_server(int port); // forward declaration

int main() {
    std::cout << "Mini-Redis server starting..." << std::endl;
    start_server(6379);
    return 0;
}
