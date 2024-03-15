#pragma once

#include "ai.h"

#include <curl/curl.h>

#include <string>
#include <vector>

class CodeprojectAiFacade final : public Ai {
public:
    CodeprojectAiFacade(std::string url, float min_confidence, const std::string& img_format);
    ~CodeprojectAiFacade();

    CodeprojectAiFacade(const CodeprojectAiFacade&) = delete;
    CodeprojectAiFacade(CodeprojectAiFacade&&) = delete;
    CodeprojectAiFacade& operator=(const CodeprojectAiFacade&) = delete;
    CodeprojectAiFacade& operator=(CodeprojectAiFacade&&) = delete;

    bool Detect(const cv::Mat& image, std::vector<Detection>& detections) override;

private:
    std::vector<unsigned char> PrepareImage(const cv::Mat& image) const;

    const std::string url_;
    const std::string min_confidence_;
    const std::string img_format_;
    const std::string img_mime_type_;
    CURL* curl_;
};
