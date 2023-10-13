#pragma once

#include "Log.h"

#include <filesystem>
#include <set>
#include <string>

enum class BufferOverflowStrategy {
    kDelay,  // Delay if buffer size is too big. Useful with media files
    kDropHalf  // Drop half of buffer
};

struct Settings {
    struct Color {
        double R = 0.0;
        double G = 0.0;
        double B = 0.0;
    };

    // General settings
    std::string source;  // Video source
    std::filesystem::path storage_path = "c:\\tmp";  // Storage for video and images
    size_t errors_before_reconnect = 5;  // Frame obtain errors before reconnect attempt
    size_t delay_after_error_ms = 2'000;  // Delay after frame obtain error
    size_t cooldown_write_time_ms = 5'000;  // Time to write after object became not detected - align this with telegram alarm notification

    BufferOverflowStrategy buffer_overflow_strategy = BufferOverflowStrategy::kDelay;

    bool use_codeproject_ai = true;
    std::string codeproject_ai_url = "http://localhost:32168/v1/vision/custom/ipcam-general";
    std::string onnx_file_path = "yolov5s.onnx";
    float min_confidence = 0.4;  // The minimum confidence level for an object will be detected. In the range 0.0 to 1.0
    int nth_detect_frame = 10;  // Perform detect on every nth frame
    bool use_image_scale = true;  // Use image scale
    double img_scale_x = 0.5;  // Scale factor before sending to AI
    double img_scale_y = 0.5;  // Scale factor before sending to AI
    std::string img_format = "jpg";  // Any cv and mime compatible type
    Color frame_color = {200.0, 0.0, 0.0};  // Color of the frame around object
    int frame_width_px = 1;  // Width of frame line
    bool use_video_scale = true;  // Scale saved videos
    int video_width = 1024;  // Scaled video width
    int video_height = 576;  // Scaled video height

    // Telegram bot preferences
    std::string bot_token;  // Keep this in secret
    std::set<uint64_t> allowed_users;  // allowed users
    size_t alarm_notification_delay_ms = 20'000;  // Delay before next telegram alarm
    bool send_video_previews = true;  // Send video preview as soon as video has been recorded
    bool send_video = false;  // Send video right after recording

    // Log options
    int log_level = LogLevel::kInfo;  // Log level
    std::string log_filename = "debug.log";  // empty string for cout
};

Settings LoadSettings(const std::string& settings_file_name);
