#pragma once

#include <iostream>
#include <mutex>
#include <string>

namespace mini_redis {

class Logger {
public:
    enum class Level { Info, Warn, Error };

    static void log(Level level, const std::string& msg) {
        static std::mutex m;
        std::lock_guard<std::mutex> lock(m);

        const char* prefix = nullptr;
        switch (level) {
        case Level::Info:  prefix = "[INFO]  "; break;
        case Level::Warn:  prefix = "[WARN]  "; break;
        case Level::Error: prefix = "[ERROR] "; break;
        }

        std::cout << prefix << msg << std::endl;
    }
};

} // namespace mini_redis
