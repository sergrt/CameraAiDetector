#pragma once

#include "CodeprojectAiFacade.h"
#include "HybridObjectDetect.h"
#include "OpenCvAiFacade.h"
#include "Settings.h"
#include "SimpleMotionDetect.h"

#include <memory>
#include <stdexcept>

inline std::unique_ptr<Ai> AiFactory(const Settings& settings) {
    if (settings.detection_engine == DetectionEngine::kCodeprojectAi) {
        return std::make_unique<CodeprojectAiFacade>(settings.codeproject_ai_url, settings.min_confidence, settings.img_format);
    } else if (settings.detection_engine == DetectionEngine::kOpenCv) {
        return std::make_unique<OpenCvAiFacade>(settings.onnx_file_path, settings.min_confidence);
    } else if (settings.detection_engine == DetectionEngine::kSimple) {
        return std::make_unique<SimpleMotionDetect>(settings.motion_detect_settings);
    } else if (settings.detection_engine == DetectionEngine::kHybridCodeprojectAi || settings.detection_engine == DetectionEngine::kHybridOpenCv) {
        return std::make_unique<HybridObjectDetect>(settings);
    } else {
        throw std::runtime_error("Unhandled detection engine in factory");
    }
}
