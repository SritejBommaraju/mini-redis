#pragma once

#include <string>
#include <vector>

namespace protocol {

    enum class CommandType {
        UNKNOWN,
        PING,
        ECHO,
        SET,
        GET
    };

    struct Command {
        CommandType type{CommandType::UNKNOWN};
        std::vector<std::string> args;
    };

    Command parse_command(const std::string& line);

}
