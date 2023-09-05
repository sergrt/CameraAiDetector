#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct Settings {
    // General settings
    std::string source;  // Video source
    std::filesystem::path storage_path = "c:\\tmp";  // Storage for video and images
    size_t errors_before_reconnect = 5;  // Frame obtain errors before reconnect attempt
    size_t delay_after_error_ms = 2'000;  // Delay after frame obtain error
    size_t cooldown_write_time_ms = 5'000;  // Time to write after object became not detected - align this with telegram alarm notification

    enum class BufferOverflowStrategy {
        Delay,
        DropHalf
    };
    BufferOverflowStrategy buffer_overflow_strategy = BufferOverflowStrategy::Delay;

    std::string codeproject_ai_url = "http://localhost:32168/v1/vision/custom/ipcam-general";
    std::string min_confidence = "0.4";  // The minimum confidence level for an object will be detected. In the range 0.0 to 1.0
    int nth_detect_frame = 10;  // Perform detect on every nth frame
    bool use_image_scale = true;  // Use image scale
    double img_scale_x = 0.5;  // Scale factor before sending to AI
    double img_scale_y = 0.5;  // Scale factor before sending to AI
    std::string img_format = "jpg";  // any cv and mime compatible type
    
    // Telegram bot preferences
    std::string bot_token;  // Keep this in secret
    std::vector<uint64_t> allowed_users; // allowed users
    size_t telegram_notification_delay_ms = 20'000;  // Delay before next telegram alarm

    // Log options
    int log_severity = 1;
    std::string log_filename = "debug.log";  // empty string for cout
};

Settings loadSettings();