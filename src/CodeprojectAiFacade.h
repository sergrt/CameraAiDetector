#pragma once

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <string>

class CodeprojectAiFacade final {
public:
    CodeprojectAiFacade(std::string url, std::string min_confidence, const std::string& img_format);
    ~CodeprojectAiFacade();

    nlohmann::json detect(const unsigned char* data, size_t data_size);

private:
    const std::string url_;
    const std::string min_confidence_;
    const std::string img_mime_type_;
    CURL* curl_;
};
