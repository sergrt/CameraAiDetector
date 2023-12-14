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


InstrumentCall::InstrumentCall(std::string name, uint64_t log_counter)
    : log_counter_(log_counter)
    , name_(std::move(name))
    , use_counter_(true) {}

InstrumentCall::InstrumentCall(std::string name, std::chrono::milliseconds log_interval)
    : log_interval_(log_interval)
    , name_(std::move(name))
    , use_counter_(false) {}

void InstrumentCall::Begin() {
    begin_time_ = std::chrono::steady_clock::now();
}

void InstrumentCall::End() {
    const auto end_time = std::chrono::steady_clock::now();
    total_ms_ += std::chrono::duration_cast<std::chrono::milliseconds>(end_time - begin_time_);

    ++counter_;
    const bool ready_to_report = use_counter_ ? (counter_ == log_counter_) : (total_ms_.count() >= log_interval_.count());
    if (ready_to_report) {
        PrintInfo();
        counter_ = 0u;
        total_ms_ = std::chrono::milliseconds(0);
    }
}

void InstrumentCall::PrintInfo() const {
    LogDebug() << "[INSTR] " << name_ << ": avg time for " << counter_ << " runs = " << total_ms_.count() / counter_ << " ms";
}
