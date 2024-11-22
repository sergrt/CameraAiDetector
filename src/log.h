#pragma once

#include "final_action.h"

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
        buffer_ += boost::lexical_cast<std::string>(data);
        return *this;
    }

    void flush() {
        stream_->flush();
    }

    std::string str() const {
        return buffer_;
    }

    bool BufferEmpty() const {
        return buffer_.empty();
    }

private:
    std::ostream* stream_{};
    std::string buffer_;
};

class Log final {
public:
    explicit Log();
    ~Log();

    Log(const Log&) = delete;
    Log(Log&&) = delete;
    Log& operator=(const Log&) = delete;
    Log& operator=(Log&&) = delete;

    template<typename T>
    Log& operator<<(const T& data) {
        stream_ << data;
        return *this;
    }

    Log& operator<<(const bool data) {
        stream_ << (data ? "true" : "false");
        return *this;
    }

    template <typename T>
    Log& operator<<(const std::vector<T>& data) {
        stream_ << "[ ";
        for (const auto& v : data)
            stream_ << v << " ";
        stream_ << "]";

        return *this;
    }

private:
    void WriteTimestamp();
    void WriteLevel();

    StreamWrapper stream_;
};

#define LOG_TRACE if (kAppLogLevel <= kTrace) Log()
#define LOG_DEBUG if (kAppLogLevel <= kDebug) Log()
#define LOG_INFO if (kAppLogLevel <= kInfo) Log()
#define LOG_WARNING if (kAppLogLevel <= kWarning) Log()
#define LOG_ERROR if (kAppLogLevel <= kError) Log()

#define LOG_FILE_LINE __FILE__ << ":" << __LINE__ << ": "

#define LOG_TRACE_EX LOG_TRACE << LOG_FILE_LINE
#define LOG_DEBUG_EX LOG_DEBUG << LOG_FILE_LINE
#define LOG_INFO_EX LOG_INFO << LOG_FILE_LINE
#define LOG_WARNING_EX LOG_WARNING << LOG_FILE_LINE
#define LOG_ERROR_EX LOG_ERROR << LOG_FILE_LINE

#define LOG_EXCEPTION(description, exception) LOG_ERROR << "Exception at " << LOG_FILE_LINE << description << ": " << exception.what()

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

// For systems with __PRETTY_FUNCTION__ it is possible to use nice funtion names in logs
/*
inline std::string GetFunctionName(const std::string& prettyFunction){
    size_t colons = prettyFunction.find("::");
    size_t begin = prettyFunction.substr(0,colons).rfind(" ") + 1;
    return prettyFunction.substr(begin);
}

#define __FULL_FUNCTION_NAME__ GetFunctionName(__PRETTY_FUNCTION__)
#define LOCATION __FULL_FUNCTION_NAME__ << ":" << __LINE__ << ": "
*/

#define LOG_VAR(x) #x << " = " << x
