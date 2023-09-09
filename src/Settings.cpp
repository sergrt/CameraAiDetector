#include "Settings.h"

#include "Helpers.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

constexpr auto settings_file_name = "settings.json";

namespace {

const std::map<std::string, BufferOverflowStrategy> strToBufferStrategy = {
    {"DELAY",    BufferOverflowStrategy::Delay},
    {"DROPHALF", BufferOverflowStrategy::DropHalf}
};

BufferOverflowStrategy stringToBufferStrategy(const std::string& str) {
    const auto it = strToBufferStrategy.find(to_upper(str));
    if (it == end(strToBufferStrategy))
        throw std::runtime_error("Unknown buffer overflow strategy string specified");
    return it->second;
}

}  // namespace

Settings loadSettings() {
    Settings settings;

    nlohmann::json json;
    {
        std::ifstream stream(settings_file_name);
        json = nlohmann::json::parse(stream);
    }

    settings.source = json["source"];  // Mandatory options are obtained like this - throwable
    settings.storage_path = json["storage_path"].get<std::string>();
    settings.errors_before_reconnect = json.value("errors_before_reconnect", settings.errors_before_reconnect);
    settings.delay_after_error_ms = json.value("delay_after_error_ms", settings.delay_after_error_ms);
    settings.cooldown_write_time_ms = json.value("cooldown_write_time_ms", settings.cooldown_write_time_ms);
    settings.buffer_overflow_strategy = stringToBufferStrategy(json.value("buffer_overflow_strategy", "Delay"));  // TODO: add str to enum

    settings.codeproject_ai_url = json["codeproject_ai_url"];
    settings.min_confidence = std::to_string(json.value("min_confidence", 0.4));
    settings.nth_detect_frame = json.value("nth_detect_frame", settings.nth_detect_frame);
    settings.use_image_scale = json.value("use_image_scale", settings.use_image_scale);
    settings.img_scale_x = json.value("img_scale_x", settings.img_scale_x);
    settings.img_scale_y = json.value("img_scale_y", settings.img_scale_y);
    settings.img_format = json.value("img_format", settings.img_format);
    
    settings.bot_token = json["bot_token"];
    settings.allowed_users = json["allowed_users"].get<std::set<uint64_t>>();
    settings.alarm_notification_delay_ms = json.value("alarm_notification_delay_ms", settings.alarm_notification_delay_ms);
    settings.send_video_previews = json.value("send_video_previews", settings.send_video_previews);

    settings.log_level = stringToLogLevel(json.value("log_level", "Info"));
    settings.log_filename = json.value("log_filename", settings.log_filename);

    return settings;
}
