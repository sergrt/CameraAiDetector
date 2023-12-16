#pragma once

#include "FinalAction.h"

#include <boost/lexical_cast.hpp>

#include <chrono>
#include <functional>
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

struct StreamWrapper {
public:
    StreamWrapper(std::ostream& stream) : stream_(&stream) {}

    template <typename T>
    StreamWrapper& operator<<(const T& data) {
        *stream_ << data;
        // Flush for immediate results. Useful for debugging
        string_ += boost::lexical_cast<std::string>(data);
        return *this;
    }

    void flush() {
        stream_->flush();
    }

    std::string str() const {
        return string_;
    }

private:
    std::ostream* stream_;
    std::string string_;
};

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

    StreamWrapper stream_;
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

struct InstrumentCall {
    InstrumentCall(std::string name, uint64_t log_counter);
    InstrumentCall(std::string name, std::chrono::milliseconds log_interval);

    [[nodiscard]] FinalAction<std::function<void()>> Trigger();
    void Begin();
    void End();
    void PrintInfo() const;

private:
    std::chrono::milliseconds total_ms_ = std::chrono::milliseconds(0);
    uint64_t counter_ = 0u;
    uint64_t log_counter_ = 100u;
    std::chrono::milliseconds log_interval_ = std::chrono::milliseconds(20'000);
    std::string name_;
    bool use_counter_ = true;
    std::chrono::time_point<std::chrono::steady_clock> begin_time_ = std::chrono::steady_clock::now();
};
