#pragma once

#include "telegram_messages.h"
#include "telegram_messages_sender.h"

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

struct Filter {
    // Using struct here - potentially this filter is extensible to different types of detects etc.
    std::chrono::minutes depth;
};

class BotFacade final {
public:
    BotFacade(const std::string& token, std::filesystem::path storage_path, std::set<uint64_t> allowed_users);
    ~BotFacade();

    BotFacade(const BotFacade&) = delete;
    BotFacade(BotFacade&&) = delete;
    BotFacade& operator=(const BotFacade&) = delete;
    BotFacade& operator=(BotFacade&&) = delete;

    void Start();
    void Stop();

    // Post to sending queue - thread safe
    void PostOnDemandPhoto(const std::filesystem::path& file_path);  // No user id - waiting users are stored in 'users_waiting_for_photo_'
    void PostAlarmPhoto(const std::filesystem::path& file_path, const std::string& classes_detected);  // No user id - goes to all users
    void PostTextMessage(const std::string& message, const std::optional<uint64_t>& user_id = std::nullopt);
    void PostVideoPreview(const std::filesystem::path& file_path, const std::optional<uint64_t>& user_id = std::nullopt);
    void PostVideo(const std::filesystem::path& file_path, const std::optional<uint64_t>& user_id = std::nullopt);
    void PostMenu(uint64_t user_id);
    void PostAnswerCallback(const std::string& callback_id);

    bool SomeoneIsWaitingForPhoto() const;

private:
    void SetupBotCommands();
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
    MessagesSender message_sender_;
    std::filesystem::path storage_path_;
    std::set<uint64_t> allowed_users_;

    std::jthread poll_thread_;
    std::atomic_bool stop_{true};

    std::set<uint64_t> users_waiting_for_photo_;
    mutable std::mutex photo_mutex_;

    std::deque<telegram::Message> messages_queue_;
    std::jthread queue_thread_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
};

}  // namespace telegram