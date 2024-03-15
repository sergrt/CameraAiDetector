#pragma once

#include "ai.h"
#include "log.h"
#include "settings.h"

#include <opencv2/opencv.hpp>

#include <vector>

class SimpleMotionDetect final : public Ai {
public:
    SimpleMotionDetect(const Settings::MotionDetectSettings& settings);

    SimpleMotionDetect(const SimpleMotionDetect&) = delete;
    SimpleMotionDetect(SimpleMotionDetect&&) = delete;
    SimpleMotionDetect& operator=(const SimpleMotionDetect&) = delete;
    SimpleMotionDetect& operator=(SimpleMotionDetect&&) = delete;

    bool Detect(const cv::Mat& image, std::vector<Detection>& detections) override;

private:
    cv::Size gaussian_sz_ = cv::Size(20, 20);
    int threshold_ = 15;
    int area_trigger_ = 150;
    InstrumentCall instrument_detect_impl_;
    cv::Mat prev_frame_;
    bool triggered_ = false;
};
