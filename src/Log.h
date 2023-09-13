#pragma once

#include <syncstream>
#include <vector>

enum LogLevel {
    kTrace,
    kDebug,
    kInfo,
    kWarning,
    kError
};

LogLevel StringToLogLevel(const std::string& str);

extern LogLevel kAppLogLevel;
extern std::ostream* kAppLogStream;

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
        if (CheckLevel())
            stream_ << data;

        return *this;
    }

    Log& operator<<(const bool data) {
        if (CheckLevel())
            stream_ << (data ? "true" : "false");

        return *this;
    }

    template <typename T>
    Log& operator<<(const std::vector<T>& data) {
        if (CheckLevel()) {
            stream_ << "[ ";
            for (const auto& v : data)
                stream_ << v << " ";
            stream_ << "]";
        }

        return *this;
    }

private:
    bool CheckLevel() const;
    void WriteTimestamp();
    void WriteLevel();

    std::osyncstream stream_;
    const LogLevel log_level_;
    bool something_written_ = false;
};

// Shortcuts, for readability
inline Log LogTrace() {
    return Log(kTrace);
}

inline Log LogDebug() {
    return Log(kDebug);
}

inline Log LogInfo() {
    return Log(kInfo);
}

inline Log LogWarning() {
    return Log(kWarning);
}

inline Log LogError() {
    return Log(kError);
}
