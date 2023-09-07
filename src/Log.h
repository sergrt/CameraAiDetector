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

class Log final {
public:
    explicit Log(LogLevel level);
    ~Log();

    Log(const Log&) = delete;
    Log(Log&&) = delete;
    Log& operator=(const Log&) = delete;
    Log& operator=(Log&&) = delete;

    template<typename T>
    Log& operator<<(const T& data) {
        if (checkLevel())
            stream_ << data;

        return *this;
    }

    Log& operator<<(const bool data) {
        if (checkLevel())
            stream_ << (data ? "true" : "false");

        return *this;
    }

private:
    bool checkLevel() const;
    void logTimestamp();
    void logLevel();

    std::osyncstream stream_;
    const LogLevel log_level_;    
    bool something_written_ = false;
};

// Shortcuts, for readability
inline Log LogTrace() {
    return Log(LL_TRACE);
}

inline Log LogInfo() {
    return Log(LL_INFO);
}

inline Log LogWarning() {
    return Log(LL_WARNING);
}

inline Log LogError() {
    return Log(LL_ERROR);
}
