#include "Logger.h"

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

Logger::Logger(LogLevel level)
    : stream_(std::osyncstream(*app_log_stream))
    , log_level_(level) {
    
    if (checkLevel()) {
        something_written_ = true;

        logTimestamp();
        logLevel();
    }
}

Logger::~Logger() {
    if (something_written_) {
        //stream_ << std::endl;
        stream_ << "\n";
    }
}

bool Logger::checkLevel() {
    return log_level_ >= app_log_level;
}

void Logger::logTimestamp() {
    const std::string timestamp = std::format("{:%Y%m%dT%H%M%S}", std::chrono::system_clock::now());
    stream_ << timestamp << " ";
}

void Logger::logLevel() {
    stream_ << logLevelToString(log_level_) << " ";
}
