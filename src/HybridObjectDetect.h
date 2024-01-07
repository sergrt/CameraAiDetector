#pragma once

#include "Ai.h"
#include "Log.h"
#include "Settings.h"
#include "SimpleMotionDetect.h"

#include <chrono>

class HybridObjectDetect final : public Ai {
public:
    HybridObjectDetect(const Settings& settings);

    HybridObjectDetect(const HybridObjectDetect&) = delete;
    HybridObjectDetect(HybridObjectDetect&&) = delete;
    HybridObjectDetect& operator=(const HybridObjectDetect&) = delete;
    HybridObjectDetect& operator=(HybridObjectDetect&&) = delete;

    bool Detect(const cv::Mat& image, std::vector<Detection>& detections) override;

private:
    std::unique_ptr<Ai> ai_;
    SimpleMotionDetect simple_motion_detect_;
    bool need_ai_proof_ = true;
    std::chrono::milliseconds min_ai_call_interval_ = std::chrono::milliseconds(1000);
    std::chrono::steady_clock::time_point prev_ai_call_ = std::chrono::steady_clock::now() - std::chrono::hours(1);
};
