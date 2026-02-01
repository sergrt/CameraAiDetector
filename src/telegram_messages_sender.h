#pragma once

#include "telegram_messages.h"

#include <tgbot/tgbot.h>

#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>

namespace telegram {
class MessagesSender final {
public:
    explicit MessagesSender(TgBot::Bot* bot, std::filesystem::path storage_path);
    ~MessagesSender();

    void operator()(const telegram::messages::TextMessage& message);
    void operator()(const telegram::messages::OnDemandPhoto& message);
    void operator()(const telegram::messages::AlarmPhoto& message);
    void operator()(const telegram::messages::Preview& message);
    void operator()(const telegram::messages::Video& message);
    void operator()(const telegram::messages::Menu& message);
    void operator()(const telegram::messages::AdminMenu& message);
    void operator()(const telegram::messages::Answer& message);

private:
    TgBot::Bot* const bot_{};
    const std::filesystem::path storage_path_;
    const TgBot::InlineKeyboardMarkup::Ptr start_menu_{};
    const TgBot::InlineKeyboardMarkup::Ptr admin_start_menu_{};

    std::deque<std::function<bool()>> resend_queue_;
    std::jthread resend_thread_;
    std::mutex resend_mutex_;
    void ResendFn(std::stop_token stop_token);
};

}  // namespace telegram
