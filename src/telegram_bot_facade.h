#pragma once

#include "telegram_messages.h"
#include "telegram_messages_sender.h"

#include <tgbot/tgbot.h>

#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>

namespace telegram {

struct Filter {
    // Using struct here - potentially this filter is extensible to different types of detects etc.
    std::chrono::minutes depth;
};

class BotFacade final {
public:
    BotFacade(const std::string& token, std::filesystem::path storage_path, std::set<uint64_t> allowed_users, std::set<uint64_t> admin_users);
    ~BotFacade();

    BotFacade(const BotFacade&) = delete;
    BotFacade(BotFacade&&) = delete;
    BotFacade& operator=(const BotFacade&) = delete;
    BotFacade& operator=(BotFacade&&) = delete;

    void Start();
    void Stop();

    // Post to sending queue - thread safe
    // user_id is the explicit recipient. If supplied and user is paused - the message still will be sent
    void PostOnDemandPhoto(const std::filesystem::path& file_path);  // No user id - waiting users are stored in 'users_waiting_for_photo_'
    void PostAlarmPhoto(const std::filesystem::path& file_path, const std::string& classes_detected);  // No user id - goes to all users
    void PostTextMessage(const std::string& message, const std::optional<uint64_t>& user_id = std::nullopt);
    void PostVideoPreview(const std::filesystem::path& file_path, const std::optional<uint64_t>& user_id = std::nullopt);
    void PostVideo(const std::filesystem::path& file_path, const std::optional<uint64_t>& user_id = std::nullopt);
    void PostMenu(uint64_t user_id);
    void PostAdminMenu(uint64_t user_id);
    void PostAnswerCallback(const std::string& callback_id);

    bool SomeoneIsWaitingForPhoto() const;

private:
    void PostStatusMessage(const std::string& message, uint64_t user_id);

    void SetupBotCommands();
    bool IsUserAllowed(uint64_t user_id) const;
    bool IsUserAdmin(uint64_t user_id) const;

    void PollThreadFunc(std::stop_token stop_token);
    void QueueThreadFunc(std::stop_token stop_token);

    void ProcessOnDemandCmd(uint64_t user_id);
    void ProcessStatusCmd(uint64_t user_id);
    void ProcessVideosCmd(uint64_t user_id, const std::optional<Filter>& filter);
    void ProcessPreviewsCmd(uint64_t user_id, const std::optional<Filter>& filter);
    void ProcessPauseCmd(uint64_t user_id, std::chrono::minutes pause_time);
    void ProcessResumeCmd(uint64_t user_id);
    void ProcessVideoCmd(uint64_t user_id, const std::string& video_uid);
    void ProcessLogCmd(uint64_t user_id);
    void ProcessSleepCmd(uint64_t user_id, std::chrono::minutes sleep_time);
    void ProcessWakeupCmd(uint64_t user_id);

    std::string PrepareStatusInfo(uint64_t requested_by);
    void UpdatePausedUsers();
    void UpdateSleepState();
    void RemoveUserFromPaused(uint64_t user_id);
    std::set<uint64_t> UpdateGetUnpausedRecipients(const std::set<uint64_t>& users, std::optional<uint64_t> requester = std::nullopt);

    std::unique_ptr<TgBot::Bot> bot_;
    MessagesSender message_sender_;
    std::filesystem::path storage_path_;
    std::set<uint64_t> allowed_users_;
    std::set<uint64_t> admin_users_;

    std::jthread poll_thread_;

    std::set<uint64_t> users_waiting_for_photo_;
    mutable std::mutex photo_mutex_;

    std::deque<Message> messages_queue_;
    std::unordered_map<uint64_t, std::chrono::zoned_time<std::chrono::system_clock::duration>> paused_users_;
    struct SleepState {
        bool is_enabled = false;
        std::chrono::zoned_time<std::chrono::system_clock::duration> end_time;
    };
    SleepState sleep_state_{};
    std::jthread queue_thread_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
};

}  // namespace telegram
