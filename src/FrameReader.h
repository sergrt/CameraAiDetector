#pragma once

#include "StreamProperties.h"

#include <opencv2/opencv.hpp>

#include <memory>
#include <optional>
#include <string>

class FrameReader final {
public:
    explicit FrameReader(std::string source);

    bool Open();
    bool Reconnect();

    bool GetFrame(cv::Mat& frame);
    StreamProperties GetStreamProperties() const;

private:
    const std::string source_;
    std::unique_ptr<cv::VideoCapture> capture_;
    mutable std::optional<StreamProperties> stream_properties_;
};
