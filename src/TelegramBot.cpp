#include "TelegramBot.h"

#include "Helpers.h"
#include "Log.h"
#include "UidUtils.h"
#include "VideoWriter.h"

#include <algorithm>
#include <filesystem>
#include <regex>

constexpr size_t max_tg_message_len = 4096;
const auto previews_cmd = std::string("previews");
const auto videos_cmd = std::string("videos");
const auto video_cmd = std::string("video");
const auto image_cmd = std::string("image");
const auto ping_cmd = std::string("ping");

namespace {

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

TgBot::InlineKeyboardMarkup::Ptr makeStartMenu() {
    TgBot::InlineKeyboardMarkup::Ptr keyboard = std::make_shared<TgBot::InlineKeyboardMarkup>();
    {
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Views 1h";
        row.back()->callbackData = "/" + previews_cmd + " 1h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Views 12h";
        row.back()->callbackData = "/" + previews_cmd + " 12h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Views 24h";
        row.back()->callbackData = "/" + previews_cmd + " 24h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Views all";
        row.back()->callbackData = "/" + previews_cmd;
        keyboard->inlineKeyboard.push_back(std::move(row));
    }
    {
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Videos 1h";
        row.back()->callbackData = "/" + videos_cmd + " 1h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Videos 12h";
        row.back()->callbackData = "/" + videos_cmd + " 12h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Videos 24h";
        row.back()->callbackData = "/" + videos_cmd + " 24h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Videos all";
        row.back()->callbackData = "/" + videos_cmd;
        keyboard->inlineKeyboard.push_back(std::move(row));
    }
    {
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Instant image";
        row.back()->callbackData = "/" + image_cmd;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Ping";
        row.back()->callbackData = "/" + ping_cmd;
        keyboard->inlineKeyboard.push_back(std::move(row));
    }
    return keyboard;
}

size_t getFileSize(const std::filesystem::path& file_name) {
    return static_cast<size_t>(std::filesystem::file_size(file_name) / 1'000'000);
}

}  // namespace

TelegramBot::TelegramBot(const std::string& token, std::filesystem::path storage_path, std::set<uint64_t> allowed_users)
    : bot_(std::make_unique<TgBot::Bot>(token))
    , storage_path_(std::move(storage_path))
    , allowed_users_(std::move(allowed_users)) {

    bot_->getEvents().onCommand("start", [this](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; isUserAllowed(id)) {
            if (!bot_->getApi().sendMessage(id, "Frequent commands", false, 0, makeStartMenu()))
                LogError() << "/start reply send failed to user " << id;
        }
    });
    bot_->getEvents().onCommand(image_cmd, [&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; isUserAllowed(id)) {
            processOnDemandCmdImpl(id);
        }
    });
    bot_->getEvents().onCommand(ping_cmd, [&](TgBot::Message::Ptr message) { 
        if (const auto id = message->chat->id; isUserAllowed(id)) {
            processPingCmdImpl(id);
        }
    });
    bot_->getEvents().onCommand(videos_cmd, [&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; isUserAllowed(id)) {
            const auto filter = getFilter(message->text);
            processVideosCmdImpl(id, filter);
        }
    });
    bot_->getEvents().onCommand(previews_cmd, [&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; isUserAllowed(id)) {
            const auto filter = getFilter(message->text);
            processPreviewsCmdImpl(id, filter);
        }
    });
    bot_->getEvents().onAnyMessage([&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; isUserAllowed(id)) {
            if (StringTools::startsWith(message->text, videoCmdPrefix())) {
                LogInfo() << "video command received: " << message->text;
                const std::string uid = message->text.substr(videoCmdPrefix().size());  // uid of file
                processVideoCmdImpl(id, uid);
            }
        } else {
            LogWarning() << "Unauthorized user tried to access: " << id;
        }
    });
    bot_->getEvents().onCallbackQuery([&](TgBot::CallbackQuery::Ptr query) {
        if (const auto id = query->message->chat->id; isUserAllowed(id)) {
            const auto command = query->data.substr(1);  // Remove slash
            if (StringTools::startsWith(query->data, videoCmdPrefix())) {
                const std::string video_id = query->data.substr(videoCmdPrefix().size());
                processVideoCmdImpl(id, video_id);
            } else if (StringTools::startsWith(command, previews_cmd)) {  // Space is a separator between cmd and filter
                const auto filter = getFilter(command.substr(previews_cmd.size()));
                processPreviewsCmdImpl(id, filter);
            } else if (StringTools::startsWith(command, videos_cmd)) {  // Space is a separator between cmd and filter
                const auto filter = getFilter(command.substr(videos_cmd.size()));
                processVideosCmdImpl(id, filter);
            } else if (StringTools::startsWith(command, image_cmd)) {
                processOnDemandCmdImpl(id);
            } else if (StringTools::startsWith(command, ping_cmd)) {
                processPingCmdImpl(id);
            }
        }
    });
}

TelegramBot::~TelegramBot() {
    stop();
}

void TelegramBot::processVideoCmdImpl(uint64_t user_id, const std::string& video_uid) {
    if (!isUidValid(video_uid)) {
        LogWarning() << "User " << user_id << " asked file with invalid uid: " << video_uid;
        if (!bot_->getApi().sendMessage(user_id, "Invalid file requested"))
            LogError() << videoCmdPrefix() << " reply send failed to user " << user_id;
        return;
    }
    const std::filesystem::path file_path = storage_path_ / VideoWriter::generateVideoFileName(video_uid);
    LogInfo() << "File uid extracted: " << video_uid << ", full path: " << file_path;

    if (std::filesystem::exists(file_path)) {
        const auto video = TgBot::InputFile::fromFile(file_path.generic_string(), "video/mp4");
        const auto caption = file_path.filename().generic_string();
        if (!bot_->getApi().sendVideo(user_id, video, false, 0, 0, 0, "", caption))
            LogError() << videoCmdPrefix() << " video file send failed to user " << user_id;
    } else {
        if (!bot_->getApi().sendMessage(user_id, "Invalid file specified"))
            LogError() << videoCmdPrefix() << " reply send failed to user " << user_id;
    }
}

void TelegramBot::processPreviewsCmdImpl(uint64_t user_id, const std::optional<Filter>& filter) {
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
    if (any_preview_posted) {
        postMessage(user_id, "Previews sending completed");
    } else {
        if (!bot_->getApi().sendMessage(user_id, "No files found"))
            LogError() << "/previews reply send failed to user " << user_id;
    }
}

void TelegramBot::processVideosCmdImpl(uint64_t user_id, const std::optional<Filter>& filter) {
    std::vector<std::string> commands_list;
    
    for (const auto& entry : std::filesystem::directory_iterator(storage_path_)) {
        if (VideoWriter::isVideoFile(entry.path())) {
            const auto file_name = entry.path().filename().generic_string();
            if (!filter || applyFilter(*filter, file_name)) {
                const auto file_size = getFileSize(entry);
                std::string command = videoCmdPrefix() + getUidFromFileName(file_name) + " (" + std::to_string(file_size) + " MB)\n";
                if (commands_list.empty() || commands_list.back().size() + command.size() > max_tg_message_len) {
                    commands_list.emplace_back(std::move(command));
                } else {
                    commands_list.back() += command;
                }
            }
        }
    }
    if (commands_list.empty())
        commands_list.emplace_back("No files found");

    for (const auto& cmd : commands_list) {
        if (!bot_->getApi().sendMessage(user_id, cmd))
            LogError() << "/videos reply send failed to user " << user_id;
    }
}

void TelegramBot::processOnDemandCmdImpl(uint64_t user_id) {
    std::lock_guard lock(photo_mutex_);
    users_waiting_for_photo_.insert(user_id);
}

void TelegramBot::processPingCmdImpl(uint64_t user_id) {
    const auto cur_time = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::system_clock::now()};
    const std::string timestamp = std::format("{:%Y-%m-%d %H:%M:%S}", cur_time);
    if (!bot_->getApi().sendMessage(user_id, timestamp))
        LogError() << "/ping reply send failed to user " << user_id;
}

bool TelegramBot::isUserAllowed(uint64_t user_id) const {
    return std::find(cbegin(allowed_users_), cend(allowed_users_), user_id) != cend(allowed_users_);
}

bool TelegramBot::someoneIswaitingForPhoto() const {
    return !users_waiting_for_photo_.empty();
}

bool TelegramBot::getCheckedFileFullPath(const std::string& file_name, std::filesystem::path& path) const {
    if (path = storage_path_ / file_name; !std::filesystem::exists(path)) {
        LogError() << "File " << path << " not found";
        return false;
    }
    return true;
}

void TelegramBot::sendOnDemandPhoto(const std::string& file_name) {
    std::filesystem::path path;
    if (getCheckedFileFullPath(file_name, path)) {
        const auto photo = TgBot::InputFile::fromFile(path.generic_string(), "image/jpeg");
        const auto caption = path.filename().generic_string();

        std::set<uint64_t> recipients;
        {
            std::lock_guard lock(photo_mutex_);
            std::swap(recipients, users_waiting_for_photo_);
        }

        for (const auto& user : recipients) {
            if (!bot_->getApi().sendPhoto(user, photo, caption)) {
                LogError() << "On-demand photo send failed to user " << user;
            }
        }
    }
}

void TelegramBot::sendAlarmPhoto(const std::string& file_name) {
    if (std::filesystem::path path; getCheckedFileFullPath(file_name, path)) {
        const auto photo = TgBot::InputFile::fromFile(path.generic_string(), "image/jpeg");
        const auto caption = path.filename().generic_string();
        for (const auto& user : allowed_users_) {
            if (!bot_->getApi().sendPhoto(user, photo, caption)) {
                LogError() << "Alarm photo send failed to user " << user;
            }
        }
    }
}

void TelegramBot::sendMessage(const std::set<uint64_t>& recipients, const std::string& message) {
    for (const auto& user : recipients) {
        if (!bot_->getApi().sendMessage(user, message)) {
            LogError() << "Message send failed to user " << user;
        }
    }
}

void TelegramBot::sendVideoPreview(const std::string& file_name) {
    if (std::filesystem::path path; getCheckedFileFullPath(file_name, path)) {
        const auto photo = TgBot::InputFile::fromFile(path.generic_string(), "image/jpeg");
        const auto cmd = videoCmdPrefix() + getUidFromFileName(file_name);

        TgBot::InlineKeyboardMarkup::Ptr keyboard = std::make_shared<TgBot::InlineKeyboardMarkup>();
        TgBot::InlineKeyboardButton::Ptr viewButton = std::make_shared<TgBot::InlineKeyboardButton>();

        const auto video_file = storage_path_ / VideoWriter::generateVideoFileName(getUidFromFileName(file_name));
        viewButton->text = getHumanDateTime(file_name) + " (" + std::to_string(getFileSize(video_file)) + " MB)";
        viewButton->callbackData = cmd;
        keyboard->inlineKeyboard.push_back({viewButton});

        for (const auto& user : allowed_users_) {
            if (!bot_->getApi().sendPhoto(user, photo, "", 0, keyboard, "", true)) {  // NOTE: No notification here
                LogError() << "Video preview send failed to user " << user;
            }
        }
    }
}

void TelegramBot::postOnDemandPhoto(const std::string& file_name) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::ON_DEMAND_PHOTO, "", file_name);
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
        notification_queue_.emplace_back(NotificationQueueItem::Type::MESSAGE, message, "", std::set<uint64_t>{ user_id });
    }
    queue_cv_.notify_one();
}

void TelegramBot::postVideoPreview(const std::string& file_name, const std::string& video_uid) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::PREVIEW, "", file_name);
    }
    queue_cv_.notify_one();
}

std::string TelegramBot::videoCmdPrefix() {
    auto video_prefix = "/" + video_cmd + "_";
    return video_prefix;
}

void TelegramBot::pollThreadFunc() {
    if (!bot_->getApi().deleteWebhook()) {
        LogError() << "Unable to delete bot Webhook";
    }
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

        const auto item = notification_queue_.front();
        notification_queue_.pop_front();
        lock.unlock();

        if (item.type == NotificationQueueItem::Type::MESSAGE) {
            sendMessage(item.recipients, item.message);
        } else if (item.type == NotificationQueueItem::Type::ON_DEMAND_PHOTO) {
            sendOnDemandPhoto(item.file_name);
        } else if (item.type == NotificationQueueItem::Type::ALARM_PHOTO) {
            sendAlarmPhoto(item.file_name);
        } else if (item.type == NotificationQueueItem::Type::PREVIEW) {
            sendVideoPreview(item.file_name);
        }
    }
}

void TelegramBot::start() {
    if (!stop_) {
        LogInfo() << "Attempt start() on already running bot";
        return;
    }

    stop_ = false;
    poll_thread_ = std::jthread(&TelegramBot::pollThreadFunc, this);
    queue_thread_ = std::jthread(&TelegramBot::queueThreadFunc, this);
}

void TelegramBot::stop() {
    if (stop_) {
        LogInfo() << "Attempt stop() on already stopped bot";
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
