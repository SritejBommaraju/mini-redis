#pragma once

#include <string>
#include <vector>
#include <optional>

class RespResult {
public:
    bool complete;
    std::vector<std::string> command;
    std::string error;
    
    RespResult(bool c, std::vector<std::string> cmd, std::string err)
        : complete(c), command(std::move(cmd)), error(std::move(err)) {}
};

class RespParser {
private:
    std::string buffer;
    
    // Reads exactly n bytes, returns empty optional if incomplete
    std::optional<std::string> readBytes(size_t n);
    
    // Reads until CRLF, returns string without CRLF
    std::optional<std::string> readLine();

public:
    RespParser();
    
    void append(const char* data, size_t len);
    
    RespResult parse();
};
