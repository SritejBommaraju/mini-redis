#include "commands.hpp"
#include "../storage/aof_logger.hpp"
#include "replication.hpp"
#include <ctime>
#include <sstream>

namespace mini_redis {
namespace commands {

// Ping Command
class PingCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)cmd; (void)ctx; (void)kv; (void)client_socket;
        detail::CommandResult result;
        result.reply = resp_simple("PONG");
        result.success = true;
        return result;
    }
};

// Echo Command
class EchoCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)ctx; (void)kv; (void)client_socket;
        detail::CommandResult result;
        if (cmd.args.empty()) {
            result.reply = resp_err("ECHO requires a message");
            result.success = false;
        } else {
            result.reply = resp_bulk(cmd.args[0]);
            result.success = true;
        }
        return result;
    }
};

// Set Command
class SetCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)ctx; (void)client_socket;
        detail::CommandResult result;
        if (cmd.args.size() < 2) {
            result.reply = resp_err("SET requires key and value");
            result.success = false;
        } else {
            kv.set(cmd.args[0], cmd.args[1]);
            result.reply = resp_simple("OK");
            result.success = true;

            if (mini_redis::g_aof_logger) {
                mini_redis::g_aof_logger->append(cmd);
            }
            if (mini_redis::g_replication_manager) {
                mini_redis::g_replication_manager->replicate_command(cmd);
            }
        }
        return result;
    }
};

// Get Command
class GetCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)ctx; (void)client_socket;
        detail::CommandResult result;
        if (cmd.args.empty()) {
            result.reply = resp_err("GET requires a key");
            result.success = false;
        } else {
            // Check for wrong type
            if (kv.type(cmd.args[0]) == KVStore::KeyType::Hash) {
                result.reply = resp_err("WRONGTYPE Operation against a key holding the wrong kind of value");
                result.success = false;
                return result;
            }

            std::string value;
            if (kv.get(cmd.args[0], value)) {
                result.reply = resp_bulk(value);
                result.success = true;
            } else {
                result.reply = resp_nil();
                result.success = true;
            }
        }
        return result;
    }
};

// Del Command
class DelCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)ctx; (void)client_socket;
        detail::CommandResult result;
        if (cmd.args.empty()) {
            result.reply = resp_err("DEL requires a key");
            result.success = false;
        } else {
            bool removed = kv.del(cmd.args[0]);
            result.reply = resp_integer(removed ? 1 : 0);
            result.success = true;
            if (mini_redis::g_aof_logger && removed) {
                mini_redis::g_aof_logger->append(cmd);
            }
            if (mini_redis::g_replication_manager && removed) {
                mini_redis::g_replication_manager->replicate_command(cmd);
            }
        }
        return result;
    }
};

// Exists Command
class ExistsCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)ctx; (void)client_socket;
        detail::CommandResult result;
        if (cmd.args.empty()) {
            result.reply = resp_err("EXISTS requires a key");
            result.success = false;
        } else {
            bool exists = kv.exists(cmd.args[0]);
            result.reply = resp_integer(exists ? 1 : 0);
            result.success = true;
        }
        return result;
    }
};

// Keys Command
class KeysCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)ctx; (void)client_socket;
        detail::CommandResult result;
        if (cmd.args.empty() || cmd.args[0] != "*") {
            result.reply = resp_err("KEYS only supports wildcard *");
            result.success = false;
        } else {
            std::vector<std::string> all_keys = kv.keys();
            result.reply = resp_array(all_keys);
            result.success = true;
        }
        return result;
    }
};

// Expire Command
class ExpireCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)ctx; (void)client_socket;
        detail::CommandResult result;
        if (cmd.args.size() < 2) {
            result.reply = resp_err("EXPIRE requires key and seconds");
            result.success = false;
        } else {
            try {
                int seconds = std::stoi(cmd.args[1]);
                bool set = kv.expire(cmd.args[0], seconds);
                result.reply = resp_integer(set ? 1 : 0);
                result.success = true;
                if (mini_redis::g_aof_logger && set) {
                    mini_redis::g_aof_logger->append(cmd);
                }
                if (mini_redis::g_replication_manager && set) {
                    mini_redis::g_replication_manager->replicate_command(cmd);
                }
            } catch (...) {
                result.reply = resp_err("Invalid seconds value");
                result.success = false;
            }
        }
        return result;
    }
};

// TTL Command
class TTLCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)ctx; (void)client_socket;
        detail::CommandResult result;
        if (cmd.args.empty()) {
            result.reply = resp_err("TTL requires a key");
            result.success = false;
        } else {
            int ttl_value = kv.ttl(cmd.args[0]);
            result.reply = resp_integer(ttl_value);
            result.success = true;
        }
        return result;
    }
};

// MGet Command
class MGetCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)ctx; (void)client_socket;
        detail::CommandResult result;
        if (cmd.args.empty()) {
            result.reply = resp_err("MGET requires at least one key");
            result.success = false;
        } else {
            std::vector<std::string> values;
            for (const auto& key : cmd.args) {
                // Handle WRONGTYPE as nil for MGET? standard Redis returns nil for wrong type in MGET
                if (kv.type(key) == KVStore::KeyType::Hash) {
                    values.push_back(""); // Treated as nil
                    continue;
                }

                std::string value;
                if (kv.get(key, value)) {
                    values.push_back(value);
                } else {
                    values.push_back("");
                }
            }
            std::string mget_result = "*" + std::to_string(values.size()) + "\r\n";
            for (const auto& val : values) {
                if (val.empty()) {
                    mget_result += resp_nil();
                } else {
                    mget_result += resp_bulk(val);
                }
            }
            result.reply = mget_result;
            result.success = true;
        }
        return result;
    }
};

// Quit Command
class QuitCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)cmd; (void)ctx; (void)kv; (void)client_socket;
        detail::CommandResult result;
        result.reply = resp_simple("OK");
        result.should_quit = true;
        result.success = true;
        return result;
    }
};

// Save Command
class SaveCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)cmd; (void)ctx; (void)client_socket;
        detail::CommandResult result;
        if (kv.save_to_rdb("mini_redis_dump.rdb")) {
            result.reply = resp_simple("OK");
            result.success = true;
        } else {
            result.reply = resp_err("ERR Save failed");
            result.success = false;
        }
        return result;
    }
};

// Load Command
class LoadCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)cmd; (void)ctx; (void)client_socket;
        detail::CommandResult result;
        if (kv.load_from_rdb("mini_redis_dump.rdb")) {
            result.reply = resp_simple("OK");
            result.success = true;
        } else {
            result.reply = resp_err("ERR Load failed");
            result.success = false;
        }
        return result;
    }
};

// Select Command
class SelectCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)kv; (void)client_socket;
        detail::CommandResult result;
        if (cmd.args.empty()) {
            result.reply = resp_err("SELECT requires database number");
            result.success = false;
        } else {
            try {
                int db_num = std::stoi(cmd.args[0]);
                std::lock_guard<std::mutex> lock(mini_redis::detail::databases_mutex);
                if (db_num >= 0 && db_num < static_cast<int>(mini_redis::detail::databases.size())) {
                    ctx.db_index = db_num;
                    result.reply = resp_simple("OK");
                    result.success = true;
                } else {
                    result.reply = resp_err("Database index out of range");
                    result.success = false;
                }
            } catch (...) {
                result.reply = resp_err("Invalid database number");
                result.success = false;
            }
        }
        return result;
    }
};

// Info Command
class InfoCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)cmd; (void)ctx; (void)kv; (void)client_socket;
        detail::CommandResult result;
        time_t now = time(nullptr);
        long long uptime = now - mini_redis::detail::server_start_time;
        std::lock_guard<std::mutex> lock(mini_redis::detail::databases_mutex);
        size_t total_keys = 0;
        for (const auto& db : mini_redis::detail::databases) {
            total_keys += db.size();
        }
        std::stringstream info;
        info << "uptime:" << uptime << "\n";
        info << "total_keys:" << total_keys << "\n";
        info << "commands_processed:" << mini_redis::detail::total_commands_processed.load() << "\n";
        info << "databases:" << mini_redis::detail::databases.size() << "\n";
        result.reply = resp_bulk(info.str());
        result.success = true;
        return result;
    }
};

// Auth Command (Stub)
class AuthCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)cmd; (void)kv; (void)client_socket;
        detail::CommandResult result;
        ctx.authenticated = true;
        result.reply = resp_simple("OK");
        result.success = true;
        return result;
    }
};

// HSet Command
class HSetCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)ctx; (void)client_socket;
        detail::CommandResult result;
        if (cmd.args.size() < 3) {
            result.reply = resp_err("HSET requires key, field, and value");
            result.success = false;
        } else {
            // Check for wrong type
            if (kv.type(cmd.args[0]) == KVStore::KeyType::String) {
                result.reply = resp_err("WRONGTYPE Operation against a key holding the wrong kind of value");
                result.success = false;
                return result;
            }

            int ret = kv.hset(cmd.args[0], cmd.args[1], cmd.args[2]);
            result.reply = resp_integer(ret);
            result.success = true;
            if (mini_redis::g_aof_logger) {
                mini_redis::g_aof_logger->append(cmd);
            }
            if (mini_redis::g_replication_manager) {
                mini_redis::g_replication_manager->replicate_command(cmd);
            }
        }
        return result;
    }
};

// HGet Command
class HGetCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)ctx; (void)client_socket;
        detail::CommandResult result;
        if (cmd.args.size() < 2) {
            result.reply = resp_err("HGET requires key and field");
            result.success = false;
        } else {
            // Check for wrong type
            if (kv.type(cmd.args[0]) == KVStore::KeyType::String) {
                result.reply = resp_err("WRONGTYPE Operation against a key holding the wrong kind of value");
                result.success = false;
                return result;
            }

            std::string value;
            if (kv.hget(cmd.args[0], cmd.args[1], value)) {
                result.reply = resp_bulk(value);
                result.success = true;
            } else {
                result.reply = resp_nil();
                result.success = true;
            }
        }
        return result;
    }
};

// Subscribe Command
class SubscribeCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)kv;
        detail::CommandResult result;
        if (cmd.args.empty()) {
            result.reply = resp_err("SUBSCRIBE requires channel name");
            result.success = false;
        } else {
            std::lock_guard<std::mutex> lock(mini_redis::detail::channels_mutex);
            for (const auto& channel : cmd.args) {
                mini_redis::detail::channels[channel].insert(client_socket);
                ctx.subscribed_channels.insert(channel);
            }
            result.reply = resp_simple("OK");
            result.success = true;
        }
        return result;
    }
};

// Publish Command
class PublishCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)ctx; (void)kv; (void)client_socket;
        detail::CommandResult result;
        if (cmd.args.size() < 2) {
            result.reply = resp_err("PUBLISH requires channel and message");
            result.success = false;
        } else {
            std::lock_guard<std::mutex> lock(mini_redis::detail::channels_mutex);
            const std::string& channel = cmd.args[0];
            const std::string& message = cmd.args[1];
            auto it = mini_redis::detail::channels.find(channel);
            int subscribers = 0;
            if (it != mini_redis::detail::channels.end()) {
                for (SOCKET sub_socket : it->second) {
                    std::string pub_msg = resp_array({channel, message});
                    send(sub_socket, pub_msg.c_str(), static_cast<int>(pub_msg.size()), 0);
                    subscribers++;
                }
            }
            result.reply = resp_integer(subscribers);
            result.success = true;
        }
        return result;
    }
};


// Unknown Command
class UnknownCommand : public BaseCommand {
public:
    detail::CommandResult execute(const protocol::Command& cmd, detail::ClientContext& ctx, KVStore& kv, SOCKET client_socket) override {
        (void)cmd; (void)ctx; (void)kv; (void)client_socket;
        detail::CommandResult result;
        result.reply = resp_err("Unknown command");
        result.success = false;
        return result;
    }
};

std::unique_ptr<CommandHandler> CommandFactory::create(protocol::CommandType type) {
    switch (type) {
        case protocol::CommandType::PING: return std::make_unique<PingCommand>();
        case protocol::CommandType::ECHO: return std::make_unique<EchoCommand>();
        case protocol::CommandType::SET: return std::make_unique<SetCommand>();
        case protocol::CommandType::GET: return std::make_unique<GetCommand>();
        case protocol::CommandType::DEL: return std::make_unique<DelCommand>();
        case protocol::CommandType::EXISTS: return std::make_unique<ExistsCommand>();
        case protocol::CommandType::KEYS: return std::make_unique<KeysCommand>();
        case protocol::CommandType::EXPIRE: return std::make_unique<ExpireCommand>();
        case protocol::CommandType::TTL: return std::make_unique<TTLCommand>();
        case protocol::CommandType::MGET: return std::make_unique<MGetCommand>();
        case protocol::CommandType::QUIT: return std::make_unique<QuitCommand>();
        case protocol::CommandType::SAVE: return std::make_unique<SaveCommand>();
        case protocol::CommandType::LOAD: return std::make_unique<LoadCommand>();
        case protocol::CommandType::SELECT: return std::make_unique<SelectCommand>();
        case protocol::CommandType::INFO: return std::make_unique<InfoCommand>();
        case protocol::CommandType::AUTH: return std::make_unique<AuthCommand>();
        case protocol::CommandType::HSET: return std::make_unique<HSetCommand>();
        case protocol::CommandType::HGET: return std::make_unique<HGetCommand>();
        case protocol::CommandType::SUBSCRIBE: return std::make_unique<SubscribeCommand>();
        case protocol::CommandType::PUBLISH: return std::make_unique<PublishCommand>();
        default: return std::make_unique<UnknownCommand>();
    }
}

} // namespace commands
} // namespace mini_redis
