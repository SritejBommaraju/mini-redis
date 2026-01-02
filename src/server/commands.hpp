#pragma once

#include "server_common.hpp"
#include "../protocol/parser.hpp"
#include "../storage/kv_store.hpp"
#include <string>
#include <vector>
#include <memory>

namespace mini_redis {
namespace commands {

// Command interface
class CommandHandler {
public:
    virtual ~CommandHandler() = default;
    virtual detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) = 0;
};

// Factory to create command handlers
class CommandFactory {
public:
    static std::unique_ptr<CommandHandler> create(protocol::CommandType type);
};

// Base class for common functionality
class BaseCommand : public CommandHandler {
protected:
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
};

} // namespace commands
} // namespace mini_redis
