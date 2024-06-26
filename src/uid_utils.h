#pragma once

#include <algorithm>
#include <chrono>
#include <format>
#include <regex>
#include <sstream>
#include <string>

// UID is a timestamp
inline std::string GenerateUid() {
    const auto cur_time = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::system_clock::now()};
    std::string timestamp = std::format("{:%Y%m%dT%H%M%S}", cur_time);
    timestamp.replace(timestamp.find('.'), 1, "_");
    return timestamp;
}

inline std::chrono::time_point<std::chrono::system_clock> GetTimestampFromUid(const std::string& uid) {
    std::tm tm = {};
    const auto us_delim_pos = uid.find('_');
    std::stringstream sstream(uid.substr(0, us_delim_pos));
    sstream >> std::get_time(&tm, "%Y%m%dT%H%M%S");
    auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    /* TODO: consider this is necessary
    const auto usec_part = uid.substr(us_delim_pos + 1, uid.size() - us_delim_pos - 1);

    auto count = std::strtol(usec_part.c_str(), nullptr, 10);
    if (errno == ERANGE) {
        count = 0;  // We're ok with 0 here
    }

    tp += std::chrono::microseconds(count);  // Double check this
    */
    return tp;
}

inline std::string GenerateFileName(const std::string& prefix, std::string* uid = nullptr) {
    const auto id = GenerateUid();
    if (uid)
        *uid = id;
    return prefix + id;
}

inline std::string GetUidFromFileName(const std::string& file_name) {
    const auto dot_pos = file_name.rfind('.');
    if (dot_pos == std::string::npos)
        return {};

    // Find second underscore - it's the delimeter: prefix_someotherprefix_YYYYMMDDTHHmmSS_US.ext
    int occurence = 0;
    const auto it = std::find_if(rbegin(file_name), rend(file_name), [&occurence](const auto& c) {
        return c == '_' && ++occurence == 2;
    });

    if (it == rend(file_name))
        return {};

    const auto start = std::distance(it, rend(file_name));
    return file_name.substr(start, dot_pos - start);
}

inline bool IsUidValid(const std::string& uid) {
    static const auto uid_regex = std::regex(R"(^20[2|3]\d(0[1-9]|1[012])(0[1-9]|[12][0-9]|3[01])T(2[0-3]|[01][0-9])[0-5][0-9][0-5][0-9]_\d+$)");
    std::smatch match;
    return std::regex_match(uid, match, uid_regex);
}

inline std::string GetDateTimeString(const auto& zoned_time) {
    std::string str = std::format("{:%d-%m-%Y %H:%M:%S}", zoned_time);
    str.erase(begin(str) + str.find('.'), end(str));
    return str;
}

inline std::string GetHumanDateTime(const std::string& file_name) {
    const auto timestamp = GetTimestampFromUid(GetUidFromFileName(file_name));
    const auto tp_zoned = std::chrono::zoned_time{std::chrono::current_zone(), timestamp};
    return GetDateTimeString(tp_zoned);
}
