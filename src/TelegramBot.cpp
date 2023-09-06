#include "Logger.h"
#include "TelegramBot.h"
#include "VideoWriter.h"

#include <algorithm>
#include <filesystem>

namespace {

bool isFilenameSafe(const std::string& file_name) {
    static constexpr auto reserved_filenames = {"CLOCK$", "AUX",  "CON",  "NUL",  "PRN",  "COM1", "COM2", "COM3",
                                                "COM4",   "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2",
                                                "LPT3",   "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

    auto upper_name = file_name;
    std::transform(begin(upper_name), end(upper_name), begin(upper_name), ::toupper);
    auto found = std::any_of(begin(reserved_filenames), end(reserved_filenames),
                             [&upper_name](const auto& name) { return name == upper_name; });

    if (found)
        return false;

    found = std::any_of(begin(file_name), end(file_name), [](const auto& c) {
        static const auto forbidden_chars = {'/', '?', '<', '>', ':', '.', '\\', '!', '@', '%', '^', '*', '~', '|', '\"'};
        return std::find(cbegin(forbidden_chars), cend(forbidden_chars), c) != cend(forbidden_chars);
    });

    return !found;
}

}  // namespace

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
            bot_->getApi().sendMessage(id, "ok");
    });
    bot_->getEvents().onCommand("list_videos", [&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; isUserAllowed(id)) {
            std::string files_list;
            const auto ext = VideoWriter::getExtension();
            const auto ext_len = VideoWriter::getExtension().size();
            for (const auto& entry : std::filesystem::directory_iterator(storage_path_)) {
                if (entry.path().extension() == ext) {
                    const auto file_name = entry.path().filename().generic_string();
                    const auto file_size = static_cast<int>(std::filesystem::file_size(entry) / 1'000'000);
                    files_list += videoCmdPrefix() + file_name.substr(0, file_name.size() - ext_len) + "    " + std::to_string(file_size) + " MB\n";
                }
            }

            bot_->getApi().sendMessage(id, files_list);
        }
    });
    bot_->getEvents().onCommand("list_videos_w_previews", [&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; isUserAllowed(id)) {
            const auto ext = VideoWriter::getExtension();
            const auto ext_len = VideoWriter::getExtension().size();
            for (const auto& entry : std::filesystem::directory_iterator(storage_path_)) {
                if (entry.path().extension() == ext) {
                    const auto file_name = entry.path().filename().generic_string();
                    const auto file_size = static_cast<int>(std::filesystem::file_size(entry) / 1'000'000);
                    const auto caption = videoCmdPrefix() + file_name.substr(0, file_name.size() - ext_len) + "    " +
                                         std::to_string(file_size) + " MB\n";

                    postVideoPreview("preview_" + file_name.substr(0, file_name.size() - ext_len), caption);
                }
            }
        }
    });
    bot_->getEvents().onAnyMessage([&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; isUserAllowed(id)) {
            if (StringTools::startsWith(message->text, videoCmdPrefix())) {
                Logger(LL_INFO) << "video command received: " << message->text;
                const std::string file_name = message->text.substr(videoCmdPrefix().size());  // Without extension - id of file
                if (!isFilenameSafe(file_name)) {
                    Logger(LL_WARNING) << "User " << id << " asked suspicious filename: " << file_name;
                    bot_->getApi().sendMessage(id, "Invalid file specified");
                    return;
                }
                const std::filesystem::path file_path = storage_path_ / (file_name + VideoWriter::getExtension());
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
    TgBot::TgLongPoll longPoll(*bot_);

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
