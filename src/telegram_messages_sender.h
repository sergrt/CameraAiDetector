#pragma once

#include "telegram_messages.h"

#include <tgbot/tgbot.h>

#include <atomic>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>

namespace telegram {
class MessagesSender final {
public:
    explicit MessagesSender(TgBot::Bot* bot, std::filesystem::path storage_path);

    void operator()(const telegram::messages::TextMessage& message);
    void operator()(const telegram::messages::OnDemandPhoto& message);
    void operator()(const telegram::messages::AlarmPhoto& message);
    void operator()(const telegram::messages::Preview& message);
    void operator()(const telegram::messages::Video& message);
    void operator()(const telegram::messages::Menu& message);
    void operator()(const telegram::messages::Answer& message);

private:
    const TgBot::Bot* const bot_;
    const std::filesystem::path storage_path_;
    const TgBot::InlineKeyboardMarkup::Ptr start_menu_;
};

}  // namespace telegram
