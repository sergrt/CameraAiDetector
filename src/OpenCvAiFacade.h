#pragma once

#include "Ai.h"

#include <filesystem>
#include <vector>

class OpenCvAiFacade final : public Ai {
public:
    OpenCvAiFacade(const std::filesystem::path& onnx_path, float min_confidence);

    OpenCvAiFacade(const OpenCvAiFacade&) = delete;
    OpenCvAiFacade(OpenCvAiFacade&&) = delete;
    OpenCvAiFacade& operator=(const OpenCvAiFacade&) = delete;
    OpenCvAiFacade& operator=(OpenCvAiFacade&&) = delete;

    bool Detect(const cv::Mat& image, std::vector<Detection>& detections) override;

private:
    std::vector<Detection> DetectImpl(const cv::Mat& input_image);

    const float min_confidence_;
    cv::dnn::Net net_;
};
