#include "telegram_bot.h"

#include "helpers.h"
#include "log.h"
#include "ring_buffer.h"
#include "safe_ptr.h"
#include "translation.h"
#include "uid_utils.h"
#include "video_writer.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <regex>
#include <set>

constexpr size_t kMaxMessageLen = 4096;
const auto kStartCmd = std::string("start");
const auto kPreviewsCmd = std::string("previews");
const auto kVideosCmd = std::string("videos");
const auto kVideoCmd = std::string("video");
const auto kImageCmd = std::string("image");
const auto kPingCmd = std::string("ping");
const auto kLogCmd = std::string("log");

extern SafePtr<RingBuffer<std::string>> AppLogTail;
extern std::chrono::time_point<std::chrono::steady_clock> kStartTime;

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
    using namespace translation::menu;
    auto keyboard = std::make_shared<TgBot::InlineKeyboardMarkup>();
    {
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kViews + " 1" + kHour;
        row.back()->callbackData = "/" + kPreviewsCmd + " 1h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kViews + " 12" + kHour;
        row.back()->callbackData = "/" + kPreviewsCmd + " 12h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kViews + " 24" + kHour;
        row.back()->callbackData = "/" + kPreviewsCmd + " 24h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kViews + " " + kAll;
        row.back()->callbackData = "/" + kPreviewsCmd;
        keyboard->inlineKeyboard.push_back(std::move(row));
    }
    {
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kVideos + " 1" + kHour;
        row.back()->callbackData = "/" + kVideosCmd + " 1h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kVideos + " 12" + kHour;
        row.back()->callbackData = "/" + kVideosCmd + " 12h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kVideos + " 24" + kHour;
        row.back()->callbackData = "/" + kVideosCmd + " 24h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kVideos + " " + kAll;
        row.back()->callbackData = "/" + kVideosCmd;
        keyboard->inlineKeyboard.push_back(std::move(row));
    }
    {
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kImage;
        row.back()->callbackData = "/" + kImageCmd;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kPing;
        row.back()->callbackData = "/" + kPingCmd;
        keyboard->inlineKeyboard.push_back(std::move(row));
    }
    return keyboard;
}

size_t GetFileSizeMb(const std::filesystem::path& file_name) {
    return std::filesystem::file_size(file_name) / 1'000'000;
}

std::string GetUptime() {
    const auto diff_s = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - kStartTime).count();
    auto formatted = std::format("{:01}d {:02}:{:02}:{:02}"
        , diff_s / 86'400
        , (diff_s % 86'400) / 3'600
        , (diff_s % 3'600) / 60
        , diff_s % 60);
    return formatted;
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
    return "&#8505; " + timestamp + ",\n"
        + free_space + " MB " + translation::messages::kAvailable + ",\n"
        + GetUptime() + " " +  translation::messages::kUptime;
}

struct VideoFileInfo {
    std::string uid;
    size_t size_mb;
    bool operator<(const VideoFileInfo& other) const {
        return uid < other.uid;
    }
};

std::set<VideoFileInfo> CollectVideoFileUids(const std::filesystem::path& storage_path, const std::optional<Filter>& filter) {
    std::set<VideoFileInfo> files;
    for (const auto& entry : std::filesystem::directory_iterator(storage_path)) {
        if (VideoWriter::IsVideoFile(entry.path())) {
            const auto file_name = entry.path().filename().generic_string();
            if (!filter || ApplyFilter(*filter, file_name)) {
                files.insert({GetUidFromFileName(file_name), GetFileSizeMb(entry)});
            }
        }
    }
    return files;
}

std::chrono::system_clock::time_point GetDateTime(TgBot::Message::Ptr message) {
    return std::chrono::system_clock::time_point(std::chrono::seconds(message->date));
}

}  // namespace

TelegramBot::TelegramBot(const std::string& token, std::filesystem::path storage_path, std::set<uint64_t> allowed_users)
    : bot_(std::make_unique<TgBot::Bot>(token))
    , storage_path_(std::move(storage_path))
    , allowed_users_(std::move(allowed_users)) {

    bot_->getEvents().onCommand(kStartCmd, [this](TgBot::Message::Ptr message) {
        LogInfo() << "Received command " << kStartCmd << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            PostMenu(id);
        }
    });
    bot_->getEvents().onCommand(kImageCmd, [&](TgBot::Message::Ptr message) {
        LogInfo() << "Received command " << kImageCmd << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            ProcessOnDemandCmd(id);
        }
    });
    bot_->getEvents().onCommand(kPingCmd, [&](TgBot::Message::Ptr message) {
        LogInfo() << "Received command " << kPingCmd << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            ProcessPingCmd(id);
        }
    });
    bot_->getEvents().onCommand(kVideosCmd, [&](TgBot::Message::Ptr message) {
        LogInfo() << "Received command " << kVideosCmd << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            const auto filter = GetFilter(message->text);
            ProcessVideosCmd(id, filter);
        }
    });
    bot_->getEvents().onCommand(kPreviewsCmd, [&](TgBot::Message::Ptr message) {
        LogInfo() << "Received command " << kPreviewsCmd << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            const auto filter = GetFilter(message->text);
            ProcessPreviewsCmd(id, filter);
        }
    });
    bot_->getEvents().onCommand(kLogCmd, [&](TgBot::Message::Ptr message) {
        LogInfo() << "Received command " << kLogCmd << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            ProcessLogCmd(id);
        }
    });
    bot_->getEvents().onAnyMessage([&](TgBot::Message::Ptr message) {
        LogInfo() << "Received message " << message->text << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            if (StringTools::startsWith(message->text, VideoCmdPrefix())) {
                LogInfo() << "video command received: " << message->text;
                const std::string uid = message->text.substr(VideoCmdPrefix().size());  // uid of file
                ProcessVideoCmd(id, uid);
            }
        } else {
            LogWarning() << "Unauthorized user tried to access: " << id;
        }
    });
    bot_->getEvents().onCallbackQuery([&](TgBot::CallbackQuery::Ptr query) {
        LogInfo() << "Received callback query " << query->message->text << " from user " << query->message->chat->id << " @ " << GetDateTime(query->message);
        if (const auto id = query->message->chat->id; IsUserAllowed(id)) {
            const auto command = query->data.substr(1);  // Remove slash
            if (StringTools::startsWith(query->data, VideoCmdPrefix())) {
                const std::string video_id = query->data.substr(VideoCmdPrefix().size());
                ProcessVideoCmd(id, video_id);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, kPreviewsCmd)) {  // Space is a separator between cmd and filter
                const auto filter = GetFilter(command.substr(kPreviewsCmd.size()));
                ProcessPreviewsCmd(id, filter);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, kVideosCmd)) {  // Space is a separator between cmd and filter
                const auto filter = GetFilter(command.substr(kVideosCmd.size()));
                ProcessVideosCmd(id, filter);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, kImageCmd)) {
                ProcessOnDemandCmd(id);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, kPingCmd)) {
                ProcessPingCmd(id);
                PostAnswerCallback(query->id);
            }
        }
    });
}

TelegramBot::~TelegramBot() {
    Stop();
}

void TelegramBot::ProcessOnDemandCmd(uint64_t user_id) {
    std::lock_guard lock(photo_mutex_);
    users_waiting_for_photo_.insert(user_id);
}

void TelegramBot::ProcessPingCmd(uint64_t user_id) {
    PostMessage(PrepareStatusInfo(storage_path_), user_id);
}

void TelegramBot::ProcessVideosCmd(uint64_t user_id, const std::optional<Filter>& filter) {
    const auto files = CollectVideoFileUids(storage_path_, filter);
    if (files.empty()) {
        PostMessage(translation::messages::kNoFilesFound, user_id);
        return;
    }

    std::string commands_message;
    for (const auto& file : files) {
        std::string command = VideoCmdPrefix() + file.uid + " (" + std::to_string(file.size_mb) + " MB)\n";
        if (commands_message.size() + command.size() > kMaxMessageLen) {
            PostMessage(commands_message, user_id);
            commands_message = command;
        } else {
            commands_message += command;
        }
    }
    PostMessage(commands_message, user_id);
}

void TelegramBot::ProcessPreviewsCmd(uint64_t user_id, const std::optional<Filter>& filter) {
    const auto files = CollectVideoFileUids(storage_path_, filter);
    if (files.empty()) {
        PostMessage(translation::messages::kNoFilesFound, user_id);
        return;
    }

    for (const auto& file : files) {
        const std::filesystem::path file_path = storage_path_ / VideoWriter::GeneratePreviewFileName(file.uid);
        PostVideoPreview(file_path, user_id);
    }
    PostMessage(translation::messages::kPreviewsSendEnded, user_id);
}

void TelegramBot::ProcessVideoCmd(uint64_t user_id, const std::string& video_uid) {
    if (!IsUidValid(video_uid)) {
        LogWarning() << "User " << user_id << " asked file with invalid uid: " << video_uid;
        PostMessage(translation::messages::kInvalidFileRequested, user_id);
        return;
    }

    const std::filesystem::path file_path = storage_path_ / VideoWriter::GenerateVideoFileName(video_uid);
    LogInfo() << "File uid: " << video_uid << ", full path: " << file_path;

    if (std::filesystem::exists(file_path)) {
        PostVideo(file_path, user_id);
    } else {
        PostMessage(translation::messages::kFileNotFound, user_id);
    }
}

void TelegramBot::ProcessLogCmd(uint64_t user_id) {
    const auto log_lines = AppLogTail->dump();
    if (log_lines.empty())
        return;

    std::string message;
    for (const auto& line : log_lines) {
        if (message.size() + line.size() > kMaxMessageLen) {
            PostMessage(message, user_id);
            message = line;
        } else {
            message += line;
        }
    }

    PostMessage(message, user_id);
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
    std::lock_guard lock(photo_mutex_);
    return !users_waiting_for_photo_.empty();
}

void TelegramBot::SendOnDemandPhoto(const std::filesystem::path& file_path, const std::set<uint64_t>& recipients) {
    if (std::filesystem::exists(file_path)) {
        const auto photo = TgBot::InputFile::fromFile(file_path.generic_string(), "image/jpeg");
        const auto caption = "&#128064; " + GetHumanDateTime(file_path.filename().generic_string());  // &#128064; - eyes

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

void TelegramBot::SendAlarmPhoto(const std::filesystem::path& file_path, const std::string& classes_detected) {
    if (std::filesystem::exists(file_path)) {
        const auto photo = TgBot::InputFile::fromFile(file_path.generic_string(), "image/jpeg");
        const auto caption = "&#10071; "
            + GetHumanDateTime(file_path.filename().generic_string())  // &#10071; - red exclamation mark
            + (classes_detected.empty() ? "" : " (" + classes_detected + ")");

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

void TelegramBot::SendMessage(const std::string& message, const std::set<uint64_t>& recipients) {
    for (const auto& user : recipients) {
        try {
            if (!bot_->getApi().sendMessage(user, message, false, 0, nullptr, "HTML"))
                LogError() << "Message send failed to user " << user;
        } catch (std::exception& e) {
            LogException("Exception while sending message", __FILE__, __LINE__, e.what());
        }
    }
}

void TelegramBot::SendVideoPreview(const std::filesystem::path& file_path, const std::set<uint64_t>& recipients) {
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

void TelegramBot::SendVideo(const std::filesystem::path& file_path, const std::set<uint64_t>& recipients) {
    if (std::filesystem::exists(file_path)) {
        const auto video = TgBot::InputFile::fromFile(file_path.generic_string(), "video/mp4");
        const auto caption = "&#127910; " + GetHumanDateTime(file_path.filename().generic_string());  // &#127910; - video camera

        for (const auto& user_id : recipients) {
            try {
                if (!bot_->getApi().sendVideo(user_id, video, false, 0, 0, 0, "", caption, 0, nullptr, "HTML"))
                    LogError() << "Video file " << file_path << " send failed to user " << user_id;
            } catch (std::exception& e) {
                LogException("Exception while sending video", __FILE__, __LINE__, e.what());
            }
        }
    }
}

void TelegramBot::SendMenu(uint64_t recipient) {
    try {
        if (!bot_->getApi().sendMessage(recipient, translation::menu::kCaption, false, 0, MakeStartMenu(), "HTML"))
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
        std::set<uint64_t> recipients;
        {
            std::lock_guard lock(photo_mutex_);
            std::swap(recipients, users_waiting_for_photo_);
        }

        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::kOnDemandPhoto, "", file_path, recipients);
    }
    queue_cv_.notify_one();
}

void TelegramBot::PostAlarmPhoto(const std::filesystem::path& file_path, const std::string& classes_detected) {
    {
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::kAlarmPhoto, classes_detected, file_path);
    }
    queue_cv_.notify_one();
}

void TelegramBot::PostMessage(const std::string& message, const std::optional<uint64_t>& user_id) {
    {
        std::set<uint64_t> recipients = (user_id ? std::set<uint64_t>{*user_id} : allowed_users_);
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::kMessage, message, "", recipients);
    }
    queue_cv_.notify_one();
}

void TelegramBot::PostVideoPreview(const std::filesystem::path& file_path, const std::optional<uint64_t>& user_id) {
    {
        std::set<uint64_t> recipients = (user_id ? std::set<uint64_t>{*user_id} : allowed_users_);
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::kPreview, "", file_path, std::move(recipients));
    }
    queue_cv_.notify_one();
}

void TelegramBot::PostVideo(const std::filesystem::path& file_path, const std::optional<uint64_t>& user_id) {
    {
        std::set<uint64_t> recipients = (user_id ? std::set<uint64_t>{*user_id} : allowed_users_);
        std::lock_guard lock(queue_mutex_);
        notification_queue_.emplace_back(NotificationQueueItem::Type::kVideo, "", file_path, std::move(recipients));
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
            SendMessage(item.payload, item.recipients);
        } else if (item.type == NotificationQueueItem::Type::kOnDemandPhoto) {
            SendOnDemandPhoto(item.file_path, item.recipients);
        } else if (item.type == NotificationQueueItem::Type::kAlarmPhoto) {
            SendAlarmPhoto(item.file_path, item.payload);
        } else if (item.type == NotificationQueueItem::Type::kPreview) {
            SendVideoPreview(item.file_path, item.recipients);
        } else if (item.type == NotificationQueueItem::Type::kVideo) {
            SendVideo(item.file_path , item.recipients);
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
