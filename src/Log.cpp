#include "Log.h"

#include "Helpers.h"
#include "RingBuffer.h"
#include "SafePtr.h"

#include <chrono>
#include <map>
#include <stdexcept>
#include <string>

extern SafePtr<RingBuffer<std::string>> AppLogTail;

const std::map<LogLevel, std::string> kLevelToStr = {
    {kTrace,   "[TRACE]"},
    {kDebug,   "[DEBUG]"},
    {kInfo,    "[INFO ]"},
    {kWarning, "[WARN ]"},
    {kError,   "[ERROR]"}
};

const std::map<std::string, LogLevel> kStrToLevel = {
    {"TRACE",   kTrace},
    {"DEBUG",   kDebug},
    {"INFO",    kInfo},
    {"WARN",    kWarning},
    {"WARNING", kWarning},
    {"ERROR",   kError}
};

namespace {

std::string LogLevelToString(LogLevel log_level) {
    const auto it = kLevelToStr.find(log_level);
    if (it == end(kLevelToStr))
        throw std::runtime_error("Unknown log level value specified");
    return it->second;
}

}  // namespace

LogLevel StringToLogLevel(const std::string& str) {
    const auto it = kStrToLevel.find(ToUpper(str));
    if (it == end(kStrToLevel))
        throw std::runtime_error("Unknown log level string specified");
    return it->second;
}

Log::Log(LogLevel level)
    : stream_(*kAppLogStream)
    , log_level_(level) {
    
    if (CheckLevel()) {
        something_written_ = true;

        WriteTimestamp();
        WriteLevel();
    }
}

Log::~Log() {
    if (something_written_) {
        stream_ << "\n";
        AppLogTail->push(stream_.str());
    }
}

bool Log::CheckLevel() const {
    return log_level_ >= kAppLogLevel;
}

void Log::WriteTimestamp() {
    const auto now = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::system_clock::now()};
    const std::string timestamp = std::format("{:%Y%m%dT%H%M%S}", now);
    stream_ << timestamp << " ";
}

void Log::WriteLevel() {
    stream_ << LogLevelToString(log_level_) << " ";
}
