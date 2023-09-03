#include "Logger.h"
#include "TelegramBot.h"
#include "VideoWriter.h"

#include <algorithm>
#include <filesystem>

TelegramBot::TelegramBot(const std::string& token, std::filesystem::path storage_path, std::vector<uint64_t> allowed_users)
    : bot_(std::make_unique<TgBot::Bot>(token))
    , storage_path_(std::move(storage_path))
    , allowed_users_(std::move(allowed_users)) {

    bot_->getEvents().onCommand("start", [this](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; isUserAllowed(id))
            bot_->getApi().sendMessage(id, "Use commands from menu");
    });
    bot_->getEvents().onCommand("image", [&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; isUserAllowed(id)) {
            std::lock_guard lock(photo_mutex_);
            users_waiting_for_photo_.push_back(id);
        }
    });
    bot_->getEvents().onCommand("ping", [&](TgBot::Message::Ptr message) { 
        if (const auto id = message->chat->id; isUserAllowed(id))
            bot_->getApi().sendMessage(id, "127.0.0.1");
    });
    bot_->getEvents().onCommand("list_videos", [&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; isUserAllowed(id)) {
            std::string files_list;
            const auto ext = VideoWriter::getExtension();
            const auto ext_len = VideoWriter::getExtension().size();
            for (const auto& entry : std::filesystem::directory_iterator(storage_path_)) {
                if (entry.path().extension() == ext) {
                    const auto file_name = entry.path().filename().generic_string();
                    files_list += videoCmdPrefix() + file_name.substr(0, file_name.size() - ext_len) + "\n";
                }
            }

            bot_->getApi().sendMessage(id, files_list);
        }
    });
    bot_->getEvents().onAnyMessage([&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; isUserAllowed(id)) {
            if (StringTools::startsWith(message->text, videoCmdPrefix())) {
                Logger(LL_INFO) << "video command received: " << message->text;
                const std::string file_name = message->text.substr(videoCmdPrefix().size()) + VideoWriter::getExtension();
                // TODO: sanitize file_name to have no malicious names
                const std::filesystem::path file_path = storage_path_ / file_name;
                Logger(LL_INFO) << "Filename extracted: " << file_name << ", full path: " << file_path;

                if (std::filesystem::exists(file_path)) {
                    bot_->getApi().sendVideo(id, TgBot::InputFile::fromFile(file_path.generic_string(), "video/mp4"), false, 0, 0, 0, "", file_path.filename().generic_string());
                } else {
                    bot_->getApi().sendMessage(id, "Invalid file specified");
                }
            }
        } else {
            Logger(LL_WARNING) << "Unauthorized user tried to access: " << id;
        }
    });
}

TelegramBot::~TelegramBot() {
    stop();
}

bool TelegramBot::isUserAllowed(uint64_t user_id) const {
    return std::find(cbegin(allowed_users_), cend(allowed_users_), user_id) != cend(allowed_users_);
}

bool TelegramBot::waitingForPhoto() const {
    return !users_waiting_for_photo_.empty();
}

void TelegramBot::sendOnDemandPhoto(const std::string& file_name, const std::vector<uint64_t>& recipients) {
    std::lock_guard lock(photo_mutex_);
    const auto path = (storage_path_ / file_name);
    for (const auto& user : recipients) {
        bot_->getApi().sendPhoto(user, TgBot::InputFile::fromFile(path.generic_string(), "image/jpeg"), path.filename().generic_string());
    }
    users_waiting_for_photo_.clear();
}

void TelegramBot::sendAlarmPhoto(const std::string& file_name) {
    const auto path = (storage_path_ / file_name);
    for (const auto& user : allowed_users_) {
        bot_->getApi().sendPhoto(user, TgBot::InputFile::fromFile(path.generic_string(), "image/jpeg"), path.filename().generic_string());
    }
}

void TelegramBot::sendMessage(const std::string& message) {
    for (const auto& user : allowed_users_) {
        bot_->getApi().sendMessage(user, message);
    }
}

void TelegramBot::sendVideoPreview(const std::string& file_name, const std::string& message) {
    const auto path = (storage_path_ / file_name);
    for (const auto& user : allowed_users_) {
        bot_->getApi().sendPhoto(user, TgBot::InputFile::fromFile(path.generic_string(), "image/jpeg"), message, 0, nullptr, "", true);  // NOTE: No notification here
    }
}

void TelegramBot::postOnDemandPhoto(const std::string& file_name) {
    std::vector<uint64_t> recipients;
    {
        std::lock_guard lock(photo_mutex_);
        std::swap(recipients, users_waiting_for_photo_);
    }
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::ON_DEMAND_PHOTO, "", file_name, std::move(recipients));
    }
    queue_cv_.notify_one();
}

void TelegramBot::postAlarmPhoto(const std::string& file_name) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::ALARM_PHOTO, "", file_name);
    }
    queue_cv_.notify_one();
}

void TelegramBot::postMessage(const std::string& message) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::MESSAGE, message, "");
    }
    queue_cv_.notify_one();
}

void TelegramBot::postVideoPreview(const std::string& file_name, const std::string& message) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::PREVIEW, message, file_name);
    }
    queue_cv_.notify_one();
}

std::string TelegramBot::videoCmdPrefix() {
    constexpr auto video_prefix = "/video_";
    return video_prefix;
}

void TelegramBot::pollThreadFunc() {
    bot_->getApi().deleteWebhook();
    TgBot::TgLongPoll longPoll(*bot_.get());

    while (!stop_) {
        Logger(LL_TRACE) << "LongPoll start";
        longPoll.start();
    }
}

void TelegramBot::queueThreadFunc() {
    while (!stop_) {
        std::unique_lock lock(queue_mutex_);
        queue_cv_.wait(lock, [&] { return !notification_queue_.empty() || stop_; });
        if (stop_)
            break;

        const NotificationQueueItem item = notification_queue_.front();
        notification_queue_.pop_front();
        lock.unlock();

        if (item.type == NotificationQueueItem::Type::MESSAGE) {
            sendMessage(item.message);
        } else if (item.type == NotificationQueueItem::Type::ON_DEMAND_PHOTO) {
            sendOnDemandPhoto(item.file_name, item.recipients);
        } else if (item.type == NotificationQueueItem::Type::ALARM_PHOTO) {
            sendAlarmPhoto(item.file_name);
        } else if (item.type == NotificationQueueItem::Type::PREVIEW) {
            sendVideoPreview(item.file_name, item.message);
        }
    }
}

void TelegramBot::start() {
    if (!stop_) {
        Logger(LL_WARNING) << "Attempt start() on already running bot";
        return;
    }

    stop_ = false;
    poll_thread_ = std::jthread(&TelegramBot::pollThreadFunc, this);
    queue_thread_ = std::jthread(&TelegramBot::queueThreadFunc, this);
}

void TelegramBot::stop() {
    if (stop_) {
        Logger(LL_WARNING) << "Attempt stop() on already stopped bot";
    }
    stop_ = true;
    queue_cv_.notify_all();

    /* Uncomment this if std::thread is used instead of std::jthread
    if (poll_thread_.joinable())
        poll_thread_.join();

    if (queue_thread_.joinable())
        queue_thread_.join();

    */
}
