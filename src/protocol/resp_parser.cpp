#include "resp_parser.hpp"

#include <sstream>
#include <algorithm>
#include <cctype>

RespParser::RespParser() : buffer("") {}

void RespParser::append(const char* data, size_t len) {
    buffer.append(data, len);
}

std::optional<std::string> RespParser::readLine() {
    size_t pos = buffer.find("\r\n");
    if (pos == std::string::npos)
        return std::nullopt;
    
    std::string line = buffer.substr(0, pos);
    buffer.erase(0, pos + 2);
    return line;
}

std::optional<std::string> RespParser::readBytes(size_t n) {
    if (buffer.size() < n + 2) // need n + CRLF
        return std::nullopt;
    
    std::string data = buffer.substr(0, n);
    buffer.erase(0, n);
    
    // now must see CRLF
    if (buffer.size() < 2 || buffer[0] != '\r' || buffer[1] != '\n')
        return std::nullopt;
    
    buffer.erase(0, 2);
    return data;
}

RespResult RespParser::parse() {
    if (buffer.empty())
        return RespResult(false, {}, "");
    
    if (buffer[0] != '*')
        return RespResult(true, {}, "ERR expected array");
    
    buffer.erase(0, 1);
    
    auto countLine = readLine();
    if (!countLine)
        return RespResult(false, {}, "");
    
    int count = std::stoi(*countLine);
    if (count <= 0)
        return RespResult(true, {}, "ERR invalid array length");
    
    std::vector<std::string> elements;
    elements.reserve(count);
    
    for (int i = 0; i < count; i++) {
        if (buffer.empty())
            return RespResult(false, {}, "");
        
        if (buffer[0] != '$')
            return RespResult(true, {}, "ERR expected bulk string");
        
        buffer.erase(0, 1);
        
        auto lenLine = readLine();
        if (!lenLine)
            return RespResult(false, {}, "");
        
        int len = std::stoi(*lenLine);
        
        if (len < 0) {
            elements.push_back(""); // null bulk string
            continue;
        }
        
        auto data = readBytes(len);
        if (!data)
            return RespResult(false, {}, "");  // incomplete
        
        elements.push_back(*data);
    }
    
    // Convert first element (command name) to uppercase for command matching
    if (!elements.empty()) {
        std::string& cmd_name = elements[0];
        std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(),
            [](unsigned char c) { return std::toupper(c); });
    }
    
    return RespResult(true, std::move(elements), "");
}
