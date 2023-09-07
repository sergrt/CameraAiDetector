#include "Log.h"

#include <chrono>
#include <string>

namespace {

std::string logLevelToString(LogLevel log_level) {
    if (log_level == LL_TRACE)
        return "[TRAC]";
    if (log_level == LL_INFO)
        return "[INFO]";
    if (log_level == LL_WARNING)
        return "[WARN]";
    if (log_level == LL_ERROR)
        return "[ERRO]";

    return "[UNKN]";
}

}  // namespace

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

bool Log::checkLevel() {
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
