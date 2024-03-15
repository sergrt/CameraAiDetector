#pragma once

#include <algorithm>
#include <string>

inline std::string ToUpper(const std::string& str) {
    auto upper_str = str;
    std::transform(begin(upper_str), end(upper_str), begin(upper_str), ::toupper);
    return upper_str;
}
