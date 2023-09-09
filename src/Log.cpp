#include "Log.h"

#include "Helpers.h"

#include <chrono>
#include <map>
#include <stdexcept>
#include <string>

namespace {

const std::map<LogLevel, std::string> levelToStr = {
    {LL_TRACE,   "[TRACE]"},
    {LL_DEBUG,   "[DEBUG]"},
    {LL_INFO,    "[INFO ]"},
    {LL_WARNING, "[WARN ]"},
    {LL_ERROR,   "[ERROR]"}
};

const std::map<std::string, LogLevel> strToLevel = {
    {"TRACE",   LL_TRACE},
    {"DEBUG",   LL_DEBUG},
    {"INFO",    LL_INFO},
    {"WARN",    LL_WARNING},
    {"WARNING", LL_WARNING},
    {"ERROR",   LL_ERROR}
};

std::string logLevelToString(LogLevel log_level) {
    const auto it = levelToStr.find(log_level);
    if (it == end(levelToStr))
        throw std::runtime_error("Unknown log level value specified");
    return it->second;
}

}  // namespace

LogLevel stringToLogLevel(const std::string& str) {
    const auto it = strToLevel.find(to_upper(str));
    if (it == end(strToLevel))
        throw std::runtime_error("Unknown log level string specified");
    return it->second;
}

Log::Log(LogLevel level)
    : stream_(std::osyncstream(*app_log_stream))
    , log_level_(level) {
    
    if (checkLevel()) {
        something_written_ = true;

        logTimestamp();
        logLevel();
    }
}

Log::~Log() {
    if (something_written_) {
        // stream_ << std::endl;  // Useful for debug
        stream_ << "\n";
    }
}

bool Log::checkLevel() const {
    return log_level_ >= app_log_level;
}

void Log::logTimestamp() {
    const auto now = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::system_clock::now()};
    const std::string timestamp = std::format("{:%Y%m%dT%H%M%S}", now);
    stream_ << timestamp << " ";
}

void Log::logLevel() {
    stream_ << logLevelToString(log_level_) << " ";
}
