#include "Settings.h"

#include "Helpers.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

const std::map<std::string, BufferOverflowStrategy> kStrToBufferStrategy = {
    {"DELAY",    BufferOverflowStrategy::kDelay},
    {"DROPHALF", BufferOverflowStrategy::kDropHalf}
};

const std::map<std::string, DetectionEngine> kStrToDetectionEngine = {
    {"CODEPROJECTAI", DetectionEngine::kCodeprojectAi},
    {"OPENCV", DetectionEngine::kOpenCv},
    {"SIMPLE", DetectionEngine::kSimple},
    {"HYBRIDCODEPROJECTAI", DetectionEngine::kHybridCodeprojectAi},
    {"HYBRIDOPENCV", DetectionEngine::kHybridOpenCv},
};

namespace {

BufferOverflowStrategy StringToBufferStrategy(const std::string& str) {
    const auto it = kStrToBufferStrategy.find(ToUpper(str));
    if (it == end(kStrToBufferStrategy))
        throw std::runtime_error("Unknown buffer overflow strategy string specified");
    return it->second;
}

DetectionEngine StringToDetectionEngine(const std::string& str) {
    const auto it = kStrToDetectionEngine.find(ToUpper(str));
    if (it == end(kStrToDetectionEngine))
        throw std::runtime_error("Unknown detection engine string specified");
    return it->second;
}

}  // namespace

Settings LoadSettings(const std::string& settings_file_name) {
    Settings settings;

    nlohmann::json json;
    {
        std::ifstream stream(settings_file_name);
        json = nlohmann::json::parse(stream);
    }

    settings.source = json.at("source");  // Mandatory options are obtained like this - throwable
    settings.storage_path = json.at("storage_path").get<std::string>();
    settings.errors_before_reconnect = json.value("errors_before_reconnect", settings.errors_before_reconnect);
    settings.delay_after_error_ms = json.value("delay_after_error_ms", settings.delay_after_error_ms);
    settings.cooldown_write_time_ms = json.value("cooldown_write_time_ms", settings.cooldown_write_time_ms);
    settings.buffer_overflow_strategy = StringToBufferStrategy(json.value("buffer_overflow_strategy", "Delay"));

    settings.detection_engine = StringToDetectionEngine(json.value("detection_engine", "CodeprojectAI"));
    settings.codeproject_ai_url = json.value("codeproject_ai_url", settings.codeproject_ai_url);
    settings.onnx_file_path = json.value("onnx_file_path", settings.onnx_file_path);
    settings.min_confidence = json.value("min_confidence", 0.4);

    if (json.contains("motion_detect_settings")) {
        const auto motion_detect_settings = json["motion_detect_settings"];
        settings.motion_detect_settings = {
            motion_detect_settings.at("gaussian_blur_sz"),
            motion_detect_settings.at("threshold"),
            motion_detect_settings.at("area_trigger"),
        };
    }

    if (json.contains("hybrid_detect_settings")) {
        const auto hybrid_detect_settings = json["hybrid_detect_settings"];
        settings.hybrid_detect_settings = {
            std::chrono::milliseconds(hybrid_detect_settings.at("min_ai_call_interval_ms"))
        };
    }

    settings.nth_detect_frame = json.value("nth_detect_frame", settings.nth_detect_frame);
    settings.use_image_scale = json.value("use_image_scale", settings.use_image_scale);
    settings.img_scale_x = json.value("img_scale_x", settings.img_scale_x);
    settings.img_scale_y = json.value("img_scale_y", settings.img_scale_y);
    settings.img_format = json.value("img_format", settings.img_format);
    if (json.contains("frame_color")) {
        const auto color_json = json["frame_color"];
        settings.frame_color = {color_json.at("R"), color_json.at("G"), color_json.at("B")};
    }
    settings.frame_width_px = json.value("frame_width_px", settings.frame_width_px);
    settings.use_video_scale = json.value("use_video_scale", settings.use_video_scale);
    settings.video_width = json.value("video_width", settings.video_width);
    settings.video_height = json.value("video_height", settings.video_height );
    settings.video_codec = json.value("video_codec", settings.video_codec);
    settings.video_container = json.value("video_container", settings.video_container);
    settings.decrease_detect_rate_while_writing = json.value("decrease_detect_rate_while_writing", settings.decrease_detect_rate_while_writing);

    settings.bot_token = json.at("bot_token");
    settings.allowed_users = json.at("allowed_users").get<std::set<uint64_t>>();
    settings.alarm_notification_delay_ms = json.value("alarm_notification_delay_ms", settings.alarm_notification_delay_ms);
    settings.preview_sampling_interval_ms = std::chrono::milliseconds(json.value("preview_sampling_interval_ms", settings.preview_sampling_interval_ms.count()));
    settings.send_video_previews = json.value("send_video_previews", settings.send_video_previews);
    settings.send_video = json.value("send_video", settings.send_video);

    settings.log_level = StringToLogLevel(json.value("log_level", "Info"));
    settings.log_filename = json.value("log_filename", settings.log_filename);
    settings.notify_on_start = json.value("notify_on_start", settings.notify_on_start);

    return settings;
}
