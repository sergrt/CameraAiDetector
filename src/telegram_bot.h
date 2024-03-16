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

struct Filter {
    // Using struct here - potentially this filter is extensible to different types of detects etc.
    std::chrono::minutes depth;
};

class TelegramBot final {
public:
    TelegramBot(const std::string& token, std::filesystem::path storage_path, std::set<uint64_t> allowed_users);
    ~TelegramBot();

    TelegramBot(const TelegramBot&) = delete;
    TelegramBot(TelegramBot&&) = delete;
    TelegramBot& operator=(const TelegramBot&) = delete;
    TelegramBot& operator=(TelegramBot&&) = delete;

    void Start();
    void Stop();

    // Post to sending queue - thread safe
    void PostOnDemandPhoto(const std::filesystem::path& file_path);  // No user id - waiting users are stored in 'users_waiting_for_photo_'
    void PostAlarmPhoto(const std::filesystem::path& file_path, const std::string& classes_detected);  // No user id - goes to all users
    void PostMessage(const std::string& message, const std::optional<uint64_t>& user_id = std::nullopt);
    void PostVideoPreview(const std::filesystem::path& file_path, const std::optional<uint64_t>& user_id = std::nullopt);
    void PostVideo(const std::filesystem::path& file_path, const std::optional<uint64_t>& user_id = std::nullopt);
    void PostMenu(uint64_t user_id);
    void PostAnswerCallback(const std::string& callback_id);

    bool SomeoneIsWaitingForPhoto() const;

    static std::string VideoCmdPrefix();

    // Visitor part: message queue item processing
    void operator()(const telegram_messages::Message& message);
    void operator()(const telegram_messages::OnDemandPhoto& message);
    void operator()(const telegram_messages::AlarmPhoto& message);
    void operator()(const telegram_messages::Preview& message);
    void operator()(const telegram_messages::Video& message);
    void operator()(const telegram_messages::Menu& message);
    void operator()(const telegram_messages::Answer& message);

private:
    bool IsUserAllowed(uint64_t user_id) const;

    void PollThreadFunc();
    void QueueThreadFunc();

    void ProcessOnDemandCmd(uint64_t user_id);
    void ProcessPingCmd(uint64_t user_id);
    void ProcessVideosCmd(uint64_t user_id, const std::optional<Filter>& filter);
    void ProcessPreviewsCmd(uint64_t user_id, const std::optional<Filter>& filter);
    void ProcessVideoCmd(uint64_t user_id, const std::string& video_uid);
    void ProcessLogCmd(uint64_t user_id);

    std::unique_ptr<TgBot::Bot> bot_;
    std::filesystem::path storage_path_;
    std::set<uint64_t> allowed_users_;

    std::jthread poll_thread_;
    std::atomic_bool stop_{true};

    std::set<uint64_t> users_waiting_for_photo_;
    mutable std::mutex photo_mutex_;

    std::deque<TelegramMessage> messages_queue_;
    std::jthread queue_thread_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
};
