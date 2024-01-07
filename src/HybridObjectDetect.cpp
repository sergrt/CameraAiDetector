#include "HybridObjectDetect.h"

#include "AiFactory.h"
#include "Log.h"

HybridObjectDetect::HybridObjectDetect(const Settings& settings)
    : simple_motion_detect_(settings.motion_detect_settings)
{
    if (settings.detection_engine == DetectionEngine::kHybridCodeprojectAi) {
        ai_ = std::make_unique<CodeprojectAiFacade>(settings.codeproject_ai_url, settings.min_confidence, settings.img_format);
    } else if (settings.detection_engine == DetectionEngine::kHybridOpenCv) {
        ai_ = std::make_unique<OpenCvAiFacade>(settings.onnx_file_path, settings.min_confidence);
    }
}

bool HybridObjectDetect::Detect(const cv::Mat& image, std::vector<Detection>& detections) {
    bool detect_res = simple_motion_detect_.Detect(image, detections);

    if (!detect_res || detections.empty()) {
        need_ai_proof_ = true;
        return detect_res;
    }

    if (need_ai_proof_) {
        detect_res = ai_->Detect(image, detections);
        LogDebug() << "Call AI detect for object proof res = " << detect_res << ", detections.size() = " << detections.size();
        need_ai_proof_ = !(detect_res && !detections.empty());
    }
    
    return detect_res;
}
