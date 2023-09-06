#pragma once

#include <chrono>
#include <format>
#include <sstream>
#include <string>

// UID is a timestamp
inline std::string generateUid() {
    const std::chrono::zoned_time cur_time{std::chrono::current_zone(), std::chrono::system_clock::now()};
    std::string timestamp = std::format("{:%Y%m%dT%H%M%S}", cur_time);
    timestamp.replace(timestamp.find('.'), 1, "_");
    return timestamp;
}

inline std::chrono::time_point<std::chrono::system_clock> getTimestampFromUid(const std::string& uid) {
    std::tm tm = {};
    const auto ms_delim_pos = uid.find('_');
    std::stringstream sstream(uid.substr(0, ms_delim_pos));
    sstream >> std::get_time(&tm, "%Y%m%dT%H%M%S");
    auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    tp += std::chrono::microseconds(std::atoi(uid.substr(ms_delim_pos + 1, uid.size() - ms_delim_pos - 1).c_str()));
    return tp;
}

inline std::string generateFileName(const std::string& prefix, std::string* uid = nullptr) {
    const auto id = generateUid();
    if (uid)
        *uid = id;
    return prefix + id;
}
