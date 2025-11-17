#include "parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace protocol {

    static std::string trim(const std::string& s) {
        std::size_t start = 0;
        while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
            ++start;
        }

        if (start == s.size()) {
            return "";
        }

        std::size_t end = s.size() - 1;
        while (end > start && (s[end] == ' ' || s[end] == '\t' || s[end] == '\r' || s[end] == '\n')) {
            --end;
        }

        return s.substr(start, end - start + 1);
    }

    static std::vector<std::string> split_tokens(const std::string& line) {
        std::vector<std::string> tokens;
        std::istringstream iss(line);
        std::string token;
        while (iss >> token) {
            tokens.push_back(token);
        }
        return tokens;
    }

    static std::string to_upper(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return s;
    }

    Command parse_command(const std::string& rawLine) {
        Command cmd;

        std::string line = trim(rawLine);
        if (line.empty()) return cmd;

        std::vector<std::string> tokens = split_tokens(line);
        if (tokens.empty()) return cmd;

        std::string name = to_upper(tokens[0]);

        if (name == "PING") cmd.type = CommandType::PING;
        else if (name == "ECHO") cmd.type = CommandType::ECHO;
        else if (name == "SET") cmd.type = CommandType::SET;
        else if (name == "GET") cmd.type = CommandType::GET;
        else cmd.type = CommandType::UNKNOWN;

        if (tokens.size() > 1)
            cmd.args.assign(tokens.begin() + 1, tokens.end());

        return cmd;
    }

}
