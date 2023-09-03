#pragma once

#include <tgbot/tgbot.h>

#include <atomic>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

// TODO: Use visitor pattern with different item types to get rid of unused fields for certain items
struct NotificationQueueItem {
    enum class Type {
        MESSAGE,
        ON_DEMAND_PHOTO,
        ALARM_PHOTO,
        PREVIEW
    };

    Type type;
    std::string message;
    std::string file_name;
    std::vector<uint64_t> recipients;
};

class TelegramBot final {
public:
    TelegramBot(const std::string& token, std::filesystem::path storage_path, std::vector<uint64_t> allowed_users);
    ~TelegramBot();

    void start();
    void stop();

    void postOnDemandPhoto(const std::string& file_name);
    void postAlarmPhoto(const std::string& file_name);
    void postMessage(const std::string& message);
    void postVideoPreview(const std::string& file_name, const std::string& message);
    
    bool waitingForPhoto() const;

    static std::string VideoCmdPrefix();

private:
    void sendOnDemandPhoto(const std::string& file_name, const std::vector<uint64_t>& recipients);
    void sendAlarmPhoto(const std::string& file_name);
    void sendMessage(const std::string& message);
    void sendVideoPreview(const std::string& file_name, const std::string& message);

    void threadFunc();
    void queueThreadFunc();
    bool isUserAllowed(uint64_t user_id) const;

    std::unique_ptr<TgBot::Bot> bot_;
    std::jthread poll_thread_;
    std::atomic_bool stop_ = true;

    std::vector<uint64_t> users_waiting_for_photo_;
    std::mutex photo_mutex_;

    std::vector<uint64_t> allowed_users_;
    std::filesystem::path storage_path_;

    std::deque<NotificationQueueItem> notification_queue_;
    std::jthread queue_thread_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
};
