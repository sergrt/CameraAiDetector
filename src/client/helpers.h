#pragma once

#include <algorithm>
#include <filesystem>
#include <string>

inline std::string ToUpper(const std::string& str) {
    auto upper_str = str;
    std::transform(begin(upper_str), end(upper_str), begin(upper_str), ::toupper);
    return upper_str;
}

inline size_t GetFileSizeMb(const std::filesystem::path& file_name) {
    return std::filesystem::file_size(file_name) / 1'000'000;
}
