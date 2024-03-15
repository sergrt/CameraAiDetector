#pragma once

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

struct Detection {
    std::string class_name;
    float confidence = 0.0f;
    cv::Rect box;
};

class Ai {
public:
    virtual ~Ai() = default;

    virtual bool Detect(const cv::Mat& image, std::vector<Detection>& detections) = 0;
};
