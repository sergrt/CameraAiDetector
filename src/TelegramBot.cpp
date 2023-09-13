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

size_t getFileSizeMb(const std::filesystem::path& file_name) {
    return static_cast<size_t>(std::filesystem::file_size(file_name) / 1'000'000);
}

void logException(const std::string& description, const std::string& file, int line, const std::string& what) {
    LogError() << description << ": " << file << ":" << line << ": " << what;
}

std::string prepareStatusInfo(const std::filesystem::path& storage_path) {
    const auto cur_time = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::system_clock::now()};
    std::string timestamp = std::format("{:%d-%m-%Y %H:%M:%S}", cur_time);
    timestamp.erase(begin(timestamp) + timestamp.find('.'), end(timestamp));

    std::string free_space = std::to_string(static_cast<size_t>(std::filesystem::space(storage_path).available / 1'000'000));
    for (auto i = static_cast<int>(free_space.size()) - 3; i > 0; i -= 3) {
        free_space.insert(i, "'");
    }
    // Some useful utf chars: &#9989; &#127909; &#128247; &#128680; &#128226; &#128266; &#10071; &#128681; &#8505; &#127916; &#127910; &#128064;
    return "&#8505; " + timestamp + ", " + free_space + " MB free";
}

struct VideoFileInfo {
    std::string uid;
    size_t size_mb;
};

std::vector<VideoFileInfo> collectVideoFileUids(const std::filesystem::path& storage_path, const std::optional<Filter>& filter) {
    std::vector<VideoFileInfo> files;
    for (const auto& entry : std::filesystem::directory_iterator(storage_path)) {
        if (VideoWriter::isVideoFile(entry.path())) {
            const auto file_name = entry.path().filename().generic_string();
            if (!filter || applyFilter(*filter, file_name)) {
                files.emplace_back(getUidFromFileName(file_name), getFileSizeMb(entry));
            }
        }
    }
    return files;
}

}  // namespace

TelegramBot::TelegramBot(const std::string& token, std::filesystem::path storage_path, std::set<uint64_t> allowed_users)
    : bot_(std::make_unique<TgBot::Bot>(token))
    , storage_path_(std::move(storage_path))
    , allowed_users_(std::move(allowed_users)) {

    bot_->getEvents().onCommand("start", [this](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; isUserAllowed(id)) {
            postMenu(id);
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
                postAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, previews_cmd)) {  // Space is a separator between cmd and filter
                const auto filter = getFilter(command.substr(previews_cmd.size()));
                processPreviewsCmdImpl(id, filter);
                postAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, videos_cmd)) {  // Space is a separator between cmd and filter
                const auto filter = getFilter(command.substr(videos_cmd.size()));
                processVideosCmdImpl(id, filter);
                postAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, image_cmd)) {
                processOnDemandCmdImpl(id);
                postAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, ping_cmd)) {
                processPingCmdImpl(id);
                postAnswerCallback(query->id);
            }
        }
    });
}

TelegramBot::~TelegramBot() {
    stop();
}

void TelegramBot::processOnDemandCmdImpl(uint64_t user_id) {
    std::lock_guard lock(photo_mutex_);
    users_waiting_for_photo_.insert(user_id);
}

void TelegramBot::processPingCmdImpl(uint64_t user_id) {
    postMessage(user_id, prepareStatusInfo(storage_path_));
}

void TelegramBot::processVideosCmdImpl(uint64_t user_id, const std::optional<Filter>& filter) {
    const auto files = collectVideoFileUids(storage_path_, filter);
    if (files.empty()) {
        postMessage(user_id, "No files found");
        return;
    }

    std::string commands_message;
    for (const auto& file : files) {
        std::string command = videoCmdPrefix() + file.uid + " (" + std::to_string(file.size_mb) + " MB)\n";
        if (commands_message.size() + command.size() > max_tg_message_len) {
            postMessage(user_id, commands_message);
            commands_message = command;
        } else {
            commands_message += command;
        }
    }
    postMessage(user_id, commands_message);
}

void TelegramBot::processPreviewsCmdImpl(uint64_t user_id, const std::optional<Filter>& filter) {
    const auto files = collectVideoFileUids(storage_path_, filter);
    if (files.empty()) {
        postMessage(user_id, "No files found");
        return;
    }

    for (const auto& file : files) {
        const std::filesystem::path file_path = storage_path_ / VideoWriter::generatePreviewFileName(file.uid);
        postVideoPreview(user_id, file_path);
    }
    postMessage(user_id, "Previews sending completed");
}

void TelegramBot::processVideoCmdImpl(uint64_t user_id, const std::string& video_uid) {
    if (!isUidValid(video_uid)) {
        LogWarning() << "User " << user_id << " asked file with invalid uid: " << video_uid;
        postMessage(user_id, "Invalid file requested");
        return;
    }

    const std::filesystem::path file_path = storage_path_ / VideoWriter::generateVideoFileName(video_uid);
    LogInfo() << "File uid: " << video_uid << ", full path: " << file_path;

    if (std::filesystem::exists(file_path)) {
        postVideo(user_id, file_path);
    } else {
        postMessage(user_id, "Invalid file specified - path not found");
    }
}

bool TelegramBot::isUserAllowed(uint64_t user_id) const {
    const auto it = std::find(cbegin(allowed_users_), cend(allowed_users_), user_id);
    if (it == cend(allowed_users_)) {
        LogWarning() << "Unauthorized user access: " << user_id;
        return false;
    }
    return true;
}

bool TelegramBot::someoneIsWaitingForPhoto() const {
    return !users_waiting_for_photo_.empty();
}

void TelegramBot::sendOnDemandPhoto(const std::filesystem::path& file_path) {
    if (std::filesystem::exists(file_path)) {
        const auto photo = TgBot::InputFile::fromFile(file_path.generic_string(), "image/jpeg");
        const auto caption = "&#128064; " + getHumanDateTime(file_path.filename().generic_string());  // &#128064; - eyes

        std::set<uint64_t> recipients;
        {
            std::lock_guard lock(photo_mutex_);
            std::swap(recipients, users_waiting_for_photo_);
        }

        for (const auto& user : recipients) {
            try {
                if (!bot_->getApi().sendPhoto(user, photo, caption, 0, nullptr, "HTML"))
                    LogError() << "On-demand photo send failed to user " << user;
            } catch (std::exception& e) {
                logException("Exception while sending photo", __FILE__, __LINE__, e.what());
            }
        }
    }
}

void TelegramBot::sendAlarmPhoto(const std::filesystem::path& file_path) {
    if (std::filesystem::exists(file_path)) {
        const auto photo = TgBot::InputFile::fromFile(file_path.generic_string(), "image/jpeg");
        const auto caption = "&#10071; " + getHumanDateTime(file_path.filename().generic_string());  // &#10071; - red exclamation mark
        for (const auto& user : allowed_users_) {
            try {
                if (!bot_->getApi().sendPhoto(user, photo, caption, 0, nullptr, "HTML"))
                    LogError() << "Alarm photo send failed to user " << user;
            } catch (std::exception& e) {
                logException("Exception while sending photo", __FILE__, __LINE__, e.what());
            }
        }
    }
}

void TelegramBot::sendMessage(const std::set<uint64_t>& recipients, const std::string& message) {
    for (const auto& user : recipients) {
        try {
            if (!bot_->getApi().sendMessage(user, message, false, 0, nullptr, "HTML"))
                LogError() << "Message send failed to user " << user;
        } catch (std::exception& e) {
            logException("Exception while sending message", __FILE__, __LINE__, e.what());
        }
    }
}

void TelegramBot::sendVideoPreview(const std::set<uint64_t>& recipients, const std::filesystem::path& file_path) {
    const auto file_name = file_path.filename().generic_string();
    const auto uid = getUidFromFileName(file_name);
    const auto video_file_path = storage_path_ / VideoWriter::generateVideoFileName(uid);
    if (std::filesystem::exists(file_path) && std::filesystem::exists(video_file_path)) {
        const auto photo = TgBot::InputFile::fromFile(file_path.generic_string(), "image/jpeg");
        const auto cmd = videoCmdPrefix() + uid;

        TgBot::InlineKeyboardMarkup::Ptr keyboard = std::make_shared<TgBot::InlineKeyboardMarkup>();
        TgBot::InlineKeyboardButton::Ptr viewButton = std::make_shared<TgBot::InlineKeyboardButton>();
        
        viewButton->text = getHumanDateTime(file_name) + " (" + std::to_string(getFileSizeMb(video_file_path)) + " MB)";
        viewButton->callbackData = cmd;
        keyboard->inlineKeyboard.push_back({viewButton});

        for (const auto& user_id : recipients) {
            try {
                if (!bot_->getApi().sendPhoto(user_id, photo, "", 0, keyboard, "", true))  // NOTE: No notification here
                    LogError() << "Video preview send failed to user " << user_id;
            } catch (std::exception& e) {
                logException("Exception while sending photo", __FILE__, __LINE__, e.what());
            }
        }
    }
}

void TelegramBot::sendVideo(uint64_t recipient, const std::filesystem::path& file_path) {
    if (std::filesystem::exists(file_path)) {
        const auto video = TgBot::InputFile::fromFile(file_path.generic_string(), "video/mp4");
        const auto caption = "&#127910; " + getHumanDateTime(file_path.filename().generic_string());  // &#127910; - video camera

        try {
            if (!bot_->getApi().sendVideo(recipient, video, false, 0, 0, 0, "", caption, 0, nullptr, "HTML"))
                LogError() << "Video file " << file_path << " send failed to user " << recipient;
        } catch (std::exception& e) {
            logException("Exception while sending video", __FILE__, __LINE__, e.what());
        }
    }
}

void TelegramBot::sendMenu(uint64_t recipient) {
    try {
        if (!bot_->getApi().sendMessage(recipient, "&#9995; Start here", false, 0, makeStartMenu(), "HTML"))
            LogError() << "/start reply send failed to user " << recipient;
    } catch (std::exception& e) {
        logException("Exception while sending menu", __FILE__, __LINE__, e.what());
    }
}

void TelegramBot::sendAnswer(const std::string& callback_id) {
    try {
        if (!bot_->getApi().answerCallbackQuery(callback_id))
            LogError() << "Answer callback query send failed";
    } catch (std::exception& e) {
        // Timed-out queries trigger this exception, so this exception might be non-fatal, but still logged
        logException("Exception (non-fatal?) while sending answer callback query", __FILE__, __LINE__, e.what());
    }
}

void TelegramBot::postOnDemandPhoto(const std::filesystem::path& file_path) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::ON_DEMAND_PHOTO, "", file_path);
    }
    queue_cv_.notify_one();
}

void TelegramBot::postAlarmPhoto(const std::filesystem::path& file_path) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::ALARM_PHOTO, "", file_path);
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

void TelegramBot::postVideoPreview(std::optional<uint64_t> user_id, const std::filesystem::path& file_path) {
    {
        std::set<uint64_t> recipients = (user_id ? std::set<uint64_t>{*user_id} : allowed_users_);
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::PREVIEW, "", file_path, recipients);
    }
    queue_cv_.notify_one();
}

void TelegramBot::postVideo(uint64_t user_id, const std::filesystem::path& file_path) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::VIDEO, "", file_path, std::set<uint64_t>{user_id});
    }
    queue_cv_.notify_one();
}

void TelegramBot::postMenu(uint64_t user_id) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::MENU, "", "", std::set<uint64_t>{user_id});
    }
    queue_cv_.notify_one();
}

void TelegramBot::postAnswerCallback(const std::string& callback_id) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::ANSWER, callback_id, "", std::set<uint64_t>{});
    }
    queue_cv_.notify_one();
}


std::string TelegramBot::videoCmdPrefix() {
    auto video_prefix = "/" + video_cmd + "_";
    return video_prefix;
}

void TelegramBot::pollThreadFunc() {
    try {
        if (!bot_->getApi().deleteWebhook())
            LogError() << "Unable to delete bot Webhook";
    } catch (std::exception& e) {
        logException("Exception while prepare bot polling", __FILE__, __LINE__, e.what());
    }

    TgBot::TgLongPoll longPoll(*bot_);

    while (!stop_) {
        try {
            LogTrace() << "LongPoll start";
            longPoll.start();
        } catch (std::exception& e) {
            logException("Exception while start polling", __FILE__, __LINE__, e.what());
        }
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
            sendMessage(item.recipients, item.payload);
        } else if (item.type == NotificationQueueItem::Type::ON_DEMAND_PHOTO) {
            sendOnDemandPhoto(item.file_path);
        } else if (item.type == NotificationQueueItem::Type::ALARM_PHOTO) {
            sendAlarmPhoto(item.file_path);
        } else if (item.type == NotificationQueueItem::Type::PREVIEW) {
            sendVideoPreview(item.recipients, item.file_path);
        } else if (item.type == NotificationQueueItem::Type::VIDEO) {
            sendVideo(*item.recipients.begin(), item.file_path);
        } else if (item.type == NotificationQueueItem::Type::MENU) {
            sendMenu(*item.recipients.begin());
        } else if (item.type == NotificationQueueItem::Type::ANSWER) {
            sendAnswer(item.payload);
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
