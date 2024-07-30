#include "hybrid_object_detect.h"

#include "ai_factory.h"
#include "log.h"

HybridObjectDetect::HybridObjectDetect(const Settings& settings)
    : simple_motion_detect_(settings.motion_detect_settings)
    , min_ai_call_interval_(settings.hybrid_detect_settings.min_ai_call_interval)
    , min_ai_nth_frame_check_(settings.hybrid_detect_settings.min_ai_nth_frame_check)
{
    ai_ = AiFactory(settings.detection_engine == DetectionEngine::kHybridCodeprojectAi ? DetectionEngine::kCodeprojectAi : DetectionEngine::kOpenCv, settings);
}

bool HybridObjectDetect::Detect(const cv::Mat& image, std::vector<Detection>& detections) {
    bool detect_res = simple_motion_detect_.Detect(image, detections);

    if (!detect_res || detections.empty()) {
        need_ai_proof_ = true;
        return detect_res;
    }

    static uint64_t frame_idx = 0;
    const bool check_frame = (frame_idx++ % min_ai_nth_frame_check_ == 0);

    if (need_ai_proof_ && (check_frame || std::chrono::steady_clock::now() - prev_ai_call_ >= min_ai_call_interval_)) {
        detect_res = ai_->Detect(image, detections);
        prev_ai_call_ = std::chrono::steady_clock::now();
        LogDebug() << "AI call for object proof: " << LOG_VAR(detect_res) << ", " << LOG_VAR(detections.size());
        need_ai_proof_ = !(detect_res && !detections.empty());
    }

    return detect_res;
}
