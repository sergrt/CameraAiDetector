#include "Log.h"
#include "TelegramBot.h"
#include "UidUtils.h"
#include "VideoWriter.h"

#include <algorithm>
#include <filesystem>
#include <regex>

namespace {

std::string to_upper(const std::string& str) {
    auto upper_str = str;
    std::transform(begin(upper_str), end(upper_str), begin(upper_str), ::toupper);
    return upper_str;
}

bool isUidValid(const std::string& uid) {
    static const auto uid_regex = std::regex(R"(^20[2|3]\d(0[1-9]|1[012])(0[1-9]|[12][0-9]|3[01])T(2[0-3]|[01][0-9])[0-5][0-9][0-5][0-9]_\d{7}$)");
    std::smatch match;
    return std::regex_match(uid, match, uid_regex);
}

struct Filter {
    // Using struct here - potentially this filter is extensible to different types of detects etc.
    std::chrono::minutes depth;
};

std::optional<Filter> getFilter(const std::string& text) {
    // Filters are time depth, e.g.:
    //   10m
    //   3h
    //   1d
    static const auto filter_regex = std::regex(R"(.* (\d+)(m|M|h|H|d|D))");
    std::smatch match;
    if (std::regex_match(text, match, filter_regex)) {
        const auto period = to_upper(match[2]);

        const auto count = std::strtol(match[1].str().c_str(), nullptr, 10);
        if (count == 0 || errno == ERANGE) {
            return {};
        }

        Filter filter{};
        if (period == "M") {
            filter.depth = std::chrono::minutes(count);
        } else if (period == "H") {
            filter.depth = std::chrono::minutes(count * 60);
        } else if (period == "D") {
            filter.depth = std::chrono::minutes(count * 60 * 24);
        }
        return filter;
    }

    return {};
}

bool applyFilter(const Filter& filter, const std::string& file_name) {
    const auto uid = getUidFromFileName(file_name);
    return std::chrono::system_clock::now() - getTimestampFromUid(uid) < filter.depth;
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
    bot_->getEvents().onCommand("videos", [&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; isUserAllowed(id)) {
            std::string commands_list;
            const auto filter = getFilter(message->text);
            for (const auto& entry : std::filesystem::directory_iterator(storage_path_)) {
                if (VideoWriter::isVideoFile(entry.path())) {
                    const auto file_name = entry.path().filename().generic_string();
                    if (!filter || applyFilter(*filter, file_name)) {
                        const auto file_size = static_cast<int>(std::filesystem::file_size(entry) / 1'000'000);
                        commands_list += videoCmdPrefix() + getUidFromFileName(file_name) + "    " + std::to_string(file_size) + " MB\n";
                    }
                }
            }
            if (!commands_list.empty())
                bot_->getApi().sendMessage(id, commands_list);
            else
                bot_->getApi().sendMessage(id, "No files found");
        }
    });
    bot_->getEvents().onCommand("previews", [&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; isUserAllowed(id)) {
            const auto filter = getFilter(message->text);
            bool any_preview_posted = false;
            for (const auto& entry : std::filesystem::directory_iterator(storage_path_)) {
                if (VideoWriter::isVideoFile(entry.path())) {
                    const auto file_name = entry.path().filename().generic_string();
                    if (!filter || applyFilter(*filter, file_name)) {
                        const auto uid = getUidFromFileName(file_name);
                        postVideoPreview(VideoWriter::generatePreviewFileName(uid), uid);
                        any_preview_posted = true;
                    }
                }
            }
            if (any_preview_posted)
                postMessage(id, "Completed");
            else
                bot_->getApi().sendMessage(id, "No files found");
        }
    });
    bot_->getEvents().onAnyMessage([&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; isUserAllowed(id)) {
            if (StringTools::startsWith(message->text, videoCmdPrefix())) {
                LogInfo() << "video command received: " << message->text;
                const std::string uid = message->text.substr(videoCmdPrefix().size());  // uid of file
                if (!isUidValid(uid)) {
                    LogWarning() << "User " << id << " asked file with invalid uid: " << uid;
                    bot_->getApi().sendMessage(id, "Invalid file requested");
                    return;
                }
                const std::filesystem::path file_path = storage_path_ / VideoWriter::generateVideoFileName(uid);
                LogInfo() << "File uid extracted: " << uid << ", full path: " << file_path;

                if (std::filesystem::exists(file_path)) {
                    bot_->getApi().sendVideo(id, TgBot::InputFile::fromFile(file_path.generic_string(), "video/mp4"), false, 0, 0, 0, "", file_path.filename().generic_string());
                } else {
                    bot_->getApi().sendMessage(id, "Invalid file specified");
                }
            }
        } else {
            LogWarning() << "Unauthorized user tried to access: " << id;
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
    if (!std::filesystem::exists(path)) {  // TODO: Refactor here and on for something like getCheckedFilePath(file_name)
        LogError() << "File " << path << " is missing";
        return;
    }

    for (const auto& user : recipients) {
        bot_->getApi().sendPhoto(user, TgBot::InputFile::fromFile(path.generic_string(), "image/jpeg"), path.filename().generic_string());
    }
    users_waiting_for_photo_.clear();
}

void TelegramBot::sendAlarmPhoto(const std::string& file_name) {
    const auto path = (storage_path_ / file_name);
    if (!std::filesystem::exists(path)) {
        LogError() << "File " << path << " is missing";
        return;
    }

    for (const auto& user : allowed_users_) {
        bot_->getApi().sendPhoto(user, TgBot::InputFile::fromFile(path.generic_string(), "image/jpeg"), path.filename().generic_string());
    }
}

void TelegramBot::sendMessage(const std::vector<uint64_t>& recipients, const std::string& message) {
    for (const auto& user : recipients) {
        bot_->getApi().sendMessage(user, message);
    }
}

void TelegramBot::sendVideoPreview(const std::string& file_name, const std::string& message) {
    const auto path = (storage_path_ / file_name);
    if (!std::filesystem::exists(path)) {
        LogError() << "File " << path << " is missing";
        return;
    }

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

void TelegramBot::postMessage(uint64_t user_id, const std::string& message) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::MESSAGE, message, "", std::vector<uint64_t>{ user_id });
    }
    queue_cv_.notify_one();
}

void TelegramBot::postVideoPreview(const std::string& file_name, const std::string& video_uid) {
    const std::string message = TelegramBot::videoCmdPrefix() + video_uid;
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
        LogTrace() << "LongPoll start";
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
            sendMessage(item.recipients, item.message);
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
        LogWarning() << "Attempt start() on already running bot";
        return;
    }

    stop_ = false;
    poll_thread_ = std::jthread(&TelegramBot::pollThreadFunc, this);
    queue_thread_ = std::jthread(&TelegramBot::queueThreadFunc, this);
}

void TelegramBot::stop() {
    if (stop_) {
        LogWarning() << "Attempt stop() on already stopped bot";
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
