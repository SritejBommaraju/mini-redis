// RESP protocol parser for Mini-Redis
// Parses client commands and converts them to structured Command objects

#pragma once

#include <string>
#include <vector>

namespace protocol {

    enum class CommandType {
        UNKNOWN,
        PING,
        ECHO,
        SET,
        GET,
        DEL,
        EXISTS,
        KEYS,
        EXPIRE,
        TTL,
        MGET,
        QUIT,
        SAVE,
        LOAD,
        SELECT,
        INFO,
        SUBSCRIBE,
        PUBLISH,
        EVAL,
        AUTH,
        HSET,
        HGET
    };

    struct Command {
        CommandType type{CommandType::UNKNOWN};
        std::vector<std::string> args;
    };

    Command parse_command(const std::string& line);
    
    // Convert RESP array (from RESP parser) to Command struct
    // First element is command name (uppercase), rest are arguments
    Command command_from_resp_array(const std::vector<std::string>& args);

}
