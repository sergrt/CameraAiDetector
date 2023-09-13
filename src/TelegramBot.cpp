#include "TelegramBot.h"

#include "Helpers.h"
#include "Log.h"
#include "UidUtils.h"
#include "VideoWriter.h"

#include <algorithm>
#include <filesystem>
#include <regex>

constexpr size_t kMaxMessageLen = 4096;
const auto kPreviewsCmd = std::string("previews");
const auto kVideosCmd = std::string("videos");
const auto kVideoCmd = std::string("video");
const auto kImageCmd = std::string("image");
const auto kPingCmd = std::string("ping");

namespace {

std::optional<Filter> GetFilter(const std::string& text) {
    // Filters are time depth, e.g.:
    //   10m
    //   3h
    //   1d
    static const auto filter_regex = std::regex(R"(.* (\d+)(m|M|h|H|d|D))");
    std::smatch match;
    if (std::regex_match(text, match, filter_regex)) {
        const auto period = ToUpper(match[2]);

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

bool ApplyFilter(const Filter& filter, const std::string& file_name) {
    const auto uid = GetUidFromFileName(file_name);
    return std::chrono::system_clock::now() - GetTimestampFromUid(uid) < filter.depth;
}

TgBot::InlineKeyboardMarkup::Ptr MakeStartMenu() {
    auto keyboard = std::make_shared<TgBot::InlineKeyboardMarkup>();
    {
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Views 1h";
        row.back()->callbackData = "/" + kPreviewsCmd + " 1h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Views 12h";
        row.back()->callbackData = "/" + kPreviewsCmd + " 12h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Views 24h";
        row.back()->callbackData = "/" + kPreviewsCmd + " 24h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Views all";
        row.back()->callbackData = "/" + kPreviewsCmd;
        keyboard->inlineKeyboard.push_back(std::move(row));
    }
    {
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Videos 1h";
        row.back()->callbackData = "/" + kVideosCmd + " 1h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Videos 12h";
        row.back()->callbackData = "/" + kVideosCmd + " 12h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Videos 24h";
        row.back()->callbackData = "/" + kVideosCmd + " 24h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Videos all";
        row.back()->callbackData = "/" + kVideosCmd;
        keyboard->inlineKeyboard.push_back(std::move(row));
    }
    {
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Instant image";
        row.back()->callbackData = "/" + kImageCmd;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = "Ping";
        row.back()->callbackData = "/" + kPingCmd;
        keyboard->inlineKeyboard.push_back(std::move(row));
    }
    return keyboard;
}

size_t GetFileSizeMb(const std::filesystem::path& file_name) {
    return std::filesystem::file_size(file_name) / 1'000'000;
}

void LogException(const std::string& description, const std::string& file, int line, const std::string& what) {
    LogError() << description << ": " << file << ":" << line << ": " << what;
}

std::string PrepareStatusInfo(const std::filesystem::path& storage_path) {
    const auto cur_time = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::system_clock::now()};
    std::string timestamp = std::format("{:%d-%m-%Y %H:%M:%S}", cur_time);
    timestamp.erase(begin(timestamp) + timestamp.find('.'), end(timestamp));

    std::string free_space = std::to_string(std::filesystem::space(storage_path).available / 1'000'000);
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

std::vector<VideoFileInfo> CollectVideoFileUids(const std::filesystem::path& storage_path, const std::optional<Filter>& filter) {
    std::vector<VideoFileInfo> files;
    for (const auto& entry : std::filesystem::directory_iterator(storage_path)) {
        if (VideoWriter::IsVideoFile(entry.path())) {
            const auto file_name = entry.path().filename().generic_string();
            if (!filter || ApplyFilter(*filter, file_name)) {
                files.emplace_back(GetUidFromFileName(file_name), GetFileSizeMb(entry));
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
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            PostMenu(id);
        }
    });
    bot_->getEvents().onCommand(kImageCmd, [&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            ProcessOnDemandCmdImpl(id);
        }
    });
    bot_->getEvents().onCommand(kPingCmd, [&](TgBot::Message::Ptr message) { 
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            ProcessPingCmdImpl(id);
        }
    });
    bot_->getEvents().onCommand(kVideosCmd, [&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            const auto filter = GetFilter(message->text);
            ProcessVideosCmdImpl(id, filter);
        }
    });
    bot_->getEvents().onCommand(kPreviewsCmd, [&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            const auto filter = GetFilter(message->text);
            ProcessPreviewsCmdImpl(id, filter);
        }
    });
    bot_->getEvents().onAnyMessage([&](TgBot::Message::Ptr message) {
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            if (StringTools::startsWith(message->text, VideoCmdPrefix())) {
                LogInfo() << "video command received: " << message->text;
                const std::string uid = message->text.substr(VideoCmdPrefix().size());  // uid of file
                ProcessVideoCmdImpl(id, uid);
            }
        } else {
            LogWarning() << "Unauthorized user tried to access: " << id;
        }
    });
    bot_->getEvents().onCallbackQuery([&](TgBot::CallbackQuery::Ptr query) {
        if (const auto id = query->message->chat->id; IsUserAllowed(id)) {
            const auto command = query->data.substr(1);  // Remove slash
            if (StringTools::startsWith(query->data, VideoCmdPrefix())) {
                const std::string video_id = query->data.substr(VideoCmdPrefix().size());
                ProcessVideoCmdImpl(id, video_id);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, kPreviewsCmd)) {  // Space is a separator between cmd and filter
                const auto filter = GetFilter(command.substr(kPreviewsCmd.size()));
                ProcessPreviewsCmdImpl(id, filter);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, kVideosCmd)) {  // Space is a separator between cmd and filter
                const auto filter = GetFilter(command.substr(kVideosCmd.size()));
                ProcessVideosCmdImpl(id, filter);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, kImageCmd)) {
                ProcessOnDemandCmdImpl(id);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, kPingCmd)) {
                ProcessPingCmdImpl(id);
                PostAnswerCallback(query->id);
            }
        }
    });
}

TelegramBot::~TelegramBot() {
    Stop();
}

void TelegramBot::ProcessOnDemandCmdImpl(uint64_t user_id) {
    std::lock_guard lock(photo_mutex_);
    users_waiting_for_photo_.insert(user_id);
}

void TelegramBot::ProcessPingCmdImpl(uint64_t user_id) {
    PostMessage(user_id, PrepareStatusInfo(storage_path_));
}

void TelegramBot::ProcessVideosCmdImpl(uint64_t user_id, const std::optional<Filter>& filter) {
    const auto files = CollectVideoFileUids(storage_path_, filter);
    if (files.empty()) {
        PostMessage(user_id, "No files found");
        return;
    }

    std::string commands_message;
    for (const auto& file : files) {
        std::string command = VideoCmdPrefix() + file.uid + " (" + std::to_string(file.size_mb) + " MB)\n";
        if (commands_message.size() + command.size() > kMaxMessageLen) {
            PostMessage(user_id, commands_message);
            commands_message = command;
        } else {
            commands_message += command;
        }
    }
    PostMessage(user_id, commands_message);
}

void TelegramBot::ProcessPreviewsCmdImpl(uint64_t user_id, const std::optional<Filter>& filter) {
    const auto files = CollectVideoFileUids(storage_path_, filter);
    if (files.empty()) {
        PostMessage(user_id, "No files found");
        return;
    }

    for (const auto& file : files) {
        const std::filesystem::path file_path = storage_path_ / VideoWriter::GeneratePreviewFileName(file.uid);
        PostVideoPreview(user_id, file_path);
    }
    PostMessage(user_id, "Previews sending completed");
}

void TelegramBot::ProcessVideoCmdImpl(uint64_t user_id, const std::string& video_uid) {
    if (!IsUidValid(video_uid)) {
        LogWarning() << "User " << user_id << " asked file with invalid uid: " << video_uid;
        PostMessage(user_id, "Invalid file requested");
        return;
    }

    const std::filesystem::path file_path = storage_path_ / VideoWriter::GenerateVideoFileName(video_uid);
    LogInfo() << "File uid: " << video_uid << ", full path: " << file_path;

    if (std::filesystem::exists(file_path)) {
        PostVideo(user_id, file_path);
    } else {
        PostMessage(user_id, "Invalid file specified - path not found");
    }
}

bool TelegramBot::IsUserAllowed(uint64_t user_id) const {
    const auto it = std::find(cbegin(allowed_users_), cend(allowed_users_), user_id);
    if (it == cend(allowed_users_)) {
        LogWarning() << "Unauthorized user access: " << user_id;
        return false;
    }
    return true;
}

bool TelegramBot::SomeoneIsWaitingForPhoto() const {
    return !users_waiting_for_photo_.empty();
}

void TelegramBot::SendOnDemandPhoto(const std::filesystem::path& file_path) {
    if (std::filesystem::exists(file_path)) {
        const auto photo = TgBot::InputFile::fromFile(file_path.generic_string(), "image/jpeg");
        const auto caption = "&#128064; " + GetHumanDateTime(file_path.filename().generic_string());  // &#128064; - eyes

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
                LogException("Exception while sending photo", __FILE__, __LINE__, e.what());
            }
        }
    }
}

void TelegramBot::SendAlarmPhoto(const std::filesystem::path& file_path) {
    if (std::filesystem::exists(file_path)) {
        const auto photo = TgBot::InputFile::fromFile(file_path.generic_string(), "image/jpeg");
        const auto caption = "&#10071; " + GetHumanDateTime(file_path.filename().generic_string());  // &#10071; - red exclamation mark
        for (const auto& user : allowed_users_) {
            try {
                if (!bot_->getApi().sendPhoto(user, photo, caption, 0, nullptr, "HTML"))
                    LogError() << "Alarm photo send failed to user " << user;
            } catch (std::exception& e) {
                LogException("Exception while sending photo", __FILE__, __LINE__, e.what());
            }
        }
    }
}

void TelegramBot::SendMessage(const std::set<uint64_t>& recipients, const std::string& message) {
    for (const auto& user : recipients) {
        try {
            if (!bot_->getApi().sendMessage(user, message, false, 0, nullptr, "HTML"))
                LogError() << "Message send failed to user " << user;
        } catch (std::exception& e) {
            LogException("Exception while sending message", __FILE__, __LINE__, e.what());
        }
    }
}

void TelegramBot::SendVideoPreview(const std::set<uint64_t>& recipients, const std::filesystem::path& file_path) {
    const auto file_name = file_path.filename().generic_string();
    const auto uid = GetUidFromFileName(file_name);
    const auto video_file_path = storage_path_ / VideoWriter::GenerateVideoFileName(uid);
    if (std::filesystem::exists(file_path) && std::filesystem::exists(video_file_path)) {
        const auto photo = TgBot::InputFile::fromFile(file_path.generic_string(), "image/jpeg");
        const auto cmd = VideoCmdPrefix() + uid;

        auto keyboard = std::make_shared<TgBot::InlineKeyboardMarkup>();
        auto view_button = std::make_shared<TgBot::InlineKeyboardButton>();
        
        view_button->text = GetHumanDateTime(file_name) + " (" + std::to_string(GetFileSizeMb(video_file_path)) + " MB)";
        view_button->callbackData = cmd;
        keyboard->inlineKeyboard.push_back({view_button});

        for (const auto& user_id : recipients) {
            try {
                if (!bot_->getApi().sendPhoto(user_id, photo, "", 0, keyboard, "", true))  // NOTE: No notification here
                    LogError() << "Video preview send failed to user " << user_id;
            } catch (std::exception& e) {
                LogException("Exception while sending photo", __FILE__, __LINE__, e.what());
            }
        }
    }
}

void TelegramBot::SendVideo(uint64_t recipient, const std::filesystem::path& file_path) {
    if (std::filesystem::exists(file_path)) {
        const auto video = TgBot::InputFile::fromFile(file_path.generic_string(), "video/mp4");
        const auto caption = "&#127910; " + GetHumanDateTime(file_path.filename().generic_string());  // &#127910; - video camera

        try {
            if (!bot_->getApi().sendVideo(recipient, video, false, 0, 0, 0, "", caption, 0, nullptr, "HTML"))
                LogError() << "Video file " << file_path << " send failed to user " << recipient;
        } catch (std::exception& e) {
            LogException("Exception while sending video", __FILE__, __LINE__, e.what());
        }
    }
}

void TelegramBot::SendMenu(uint64_t recipient) {
    try {
        if (!bot_->getApi().sendMessage(recipient, "&#9995; Start here", false, 0, MakeStartMenu(), "HTML"))
            LogError() << "/start reply send failed to user " << recipient;
    } catch (std::exception& e) {
        LogException("Exception while sending menu", __FILE__, __LINE__, e.what());
    }
}

void TelegramBot::SendAnswer(const std::string& callback_id) {
    try {
        if (!bot_->getApi().answerCallbackQuery(callback_id))
            LogError() << "Answer callback query send failed";
    } catch (std::exception& e) {
        // Timed-out queries trigger this exception, so this exception might be non-fatal, but still logged
        LogException("Exception (non-fatal?) while sending answer callback query", __FILE__, __LINE__, e.what());
    }
}

void TelegramBot::PostOnDemandPhoto(const std::filesystem::path& file_path) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::kOnDemandPhoto, "", file_path);
    }
    queue_cv_.notify_one();
}

void TelegramBot::PostAlarmPhoto(const std::filesystem::path& file_path) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::kAlarmPhoto, "", file_path);
    }
    queue_cv_.notify_one();
}

void TelegramBot::PostMessage(uint64_t user_id, const std::string& message) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::kMessage, message, "", std::set<uint64_t>{ user_id });
    }
    queue_cv_.notify_one();
}

void TelegramBot::PostVideoPreview(std::optional<uint64_t> user_id, const std::filesystem::path& file_path) {
    {
        std::set<uint64_t> recipients = (user_id ? std::set<uint64_t>{*user_id} : allowed_users_);
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::kPreview, "", file_path, recipients);
    }
    queue_cv_.notify_one();
}

void TelegramBot::PostVideo(uint64_t user_id, const std::filesystem::path& file_path) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::kVideo, "", file_path, std::set<uint64_t>{user_id});
    }
    queue_cv_.notify_one();
}

void TelegramBot::PostMenu(uint64_t user_id) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::kMenu, "", "", std::set<uint64_t>{user_id});
    }
    queue_cv_.notify_one();
}

void TelegramBot::PostAnswerCallback(const std::string& callback_id) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::kAnswer, callback_id, "", std::set<uint64_t>{});
    }
    queue_cv_.notify_one();
}


std::string TelegramBot::VideoCmdPrefix() {
    auto video_prefix = "/" + kVideoCmd + "_";
    return video_prefix;
}

void TelegramBot::PollThreadFunc() {
    try {
        if (!bot_->getApi().deleteWebhook())
            LogError() << "Unable to delete bot Webhook";
    } catch (std::exception& e) {
        LogException("Exception while prepare bot polling", __FILE__, __LINE__, e.what());
    }

    TgBot::TgLongPoll long_poll(*bot_);

    while (!stop_) {
        try {
            LogTrace() << "LongPoll start";
            long_poll.start();
        } catch (std::exception& e) {
            LogException("Exception while start polling", __FILE__, __LINE__, e.what());
        }
    }
}

void TelegramBot::QueueThreadFunc() {
    while (!stop_) {
        std::unique_lock lock(queue_mutex_);
        queue_cv_.wait(lock, [&] { return !notification_queue_.empty() || stop_; });
        if (stop_)
            break;

        const auto item = notification_queue_.front();
        notification_queue_.pop_front();
        lock.unlock();

        if (item.type == NotificationQueueItem::Type::kMessage) {
            SendMessage(item.recipients, item.payload);
        } else if (item.type == NotificationQueueItem::Type::kOnDemandPhoto) {
            SendOnDemandPhoto(item.file_path);
        } else if (item.type == NotificationQueueItem::Type::kAlarmPhoto) {
            SendAlarmPhoto(item.file_path);
        } else if (item.type == NotificationQueueItem::Type::kPreview) {
            SendVideoPreview(item.recipients, item.file_path);
        } else if (item.type == NotificationQueueItem::Type::kVideo) {
            SendVideo(*item.recipients.begin(), item.file_path);
        } else if (item.type == NotificationQueueItem::Type::kMenu) {
            SendMenu(*item.recipients.begin());
        } else if (item.type == NotificationQueueItem::Type::kAnswer) {
            SendAnswer(item.payload);
        }
    }
}

void TelegramBot::Start() {
    if (!stop_) {
        LogInfo() << "Attempt start() on already running bot";
        return;
    }

    stop_ = false;
    poll_thread_ = std::jthread(&TelegramBot::PollThreadFunc, this);
    queue_thread_ = std::jthread(&TelegramBot::QueueThreadFunc, this);
}

void TelegramBot::Stop() {
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
