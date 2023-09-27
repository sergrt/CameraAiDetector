#include "Settings.h"

#include "Helpers.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

constexpr auto kSettingsFileName = "settings.json";
const std::map<std::string, BufferOverflowStrategy> kStrToBufferStrategy = {
    {"DELAY",    BufferOverflowStrategy::kDelay},
    {"DROPHALF", BufferOverflowStrategy::kDropHalf}
};

namespace {

BufferOverflowStrategy StringToBufferStrategy(const std::string& str) {
    const auto it = kStrToBufferStrategy.find(ToUpper(str));
    if (it == end(kStrToBufferStrategy))
        throw std::runtime_error("Unknown buffer overflow strategy string specified");
    return it->second;
}

}  // namespace

Settings LoadSettings() {
    Settings settings;

    nlohmann::json json;
    {
        std::ifstream stream(kSettingsFileName);
        json = nlohmann::json::parse(stream);
    }

    settings.source = json["source"];  // Mandatory options are obtained like this - throwable
    settings.storage_path = json["storage_path"].get<std::string>();
    settings.errors_before_reconnect = json.value("errors_before_reconnect", settings.errors_before_reconnect);
    settings.delay_after_error_ms = json.value("delay_after_error_ms", settings.delay_after_error_ms);
    settings.cooldown_write_time_ms = json.value("cooldown_write_time_ms", settings.cooldown_write_time_ms);
    settings.buffer_overflow_strategy = StringToBufferStrategy(json.value("buffer_overflow_strategy", "Delay"));

    settings.codeproject_ai_url = json["codeproject_ai_url"];
    settings.min_confidence = std::to_string(json.value("min_confidence", 0.4));
    settings.nth_detect_frame = json.value("nth_detect_frame", settings.nth_detect_frame);
    settings.use_image_scale = json.value("use_image_scale", settings.use_image_scale);
    settings.img_scale_x = json.value("img_scale_x", settings.img_scale_x);
    settings.img_scale_y = json.value("img_scale_y", settings.img_scale_y);
    settings.img_format = json.value("img_format", settings.img_format);
    settings.use_video_scale = json.value("use_video_scale", settings.use_video_scale);
    settings.video_width = json.value("video_width", settings.video_width);
    settings.video_height = json.value("video_height", settings.video_height );

    settings.bot_token = json["bot_token"];
    settings.allowed_users = json["allowed_users"].get<std::set<uint64_t>>();
    settings.alarm_notification_delay_ms = json.value("alarm_notification_delay_ms", settings.alarm_notification_delay_ms);
    settings.send_video_previews = json.value("send_video_previews", settings.send_video_previews);

    settings.log_level = StringToLogLevel(json.value("log_level", "Info"));
    settings.log_filename = json.value("log_filename", settings.log_filename);

    return settings;
}
