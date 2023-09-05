#pragma once

#include <syncstream>

enum LogLevel {
    // Prefixes because of names clash on Windows
    LL_TRACE,
    LL_INFO,
    LL_WARNING,
    LL_ERROR
};

extern LogLevel app_log_level;
extern std::ostream* app_log_stream;

class Logger final {
public:
    explicit Logger(LogLevel level);
    ~Logger();

    template<typename T>
    Logger& operator<<(const T& data) {
        if (checkLevel())
            stream_ << data;

        return *this;
    }

    Logger& operator<<(const bool data) {
        if (checkLevel())
            stream_ << (data ? "true" : "false");

        return *this;
    }

private:
    bool checkLevel();
    void logTimestamp();
    void logLevel();

    const LogLevel log_level_;
    std::osyncstream stream_;
    bool something_written_ = false;
};

// TODO: Consider shortcuts, like following:
// inline Logger LogInfo() {
//     return Logger(LL_INFO);
// }
