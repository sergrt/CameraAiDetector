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


extern SafePtr<RingBuffer<std::string>> AppLogTail;
extern std::chrono::time_point<std::chrono::steady_clock> kStartTime;

namespace telegram {

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

std::set<VideoFileInfo> CollectVideoFileUids(const std::filesystem::path& storage_path, const std::optional<telegram::Filter>& filter) {
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

BotFacade::BotFacade(const std::string& token, std::filesystem::path storage_path, std::set<uint64_t> allowed_users)
    : bot_{std::make_unique<TgBot::Bot>(token)},
      message_sender_{bot_.get(), storage_path},
      storage_path_{std::move(storage_path)},
      allowed_users_{std::move(allowed_users)} {
    bot_->getEvents().onCommand(telegram::commands::kStart, [this](TgBot::Message::Ptr message) {
        LogInfo() << "Received command " << telegram::commands::kStart << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            PostMenu(id);
        }
    });
    bot_->getEvents().onCommand(telegram::commands::kImage, [&](TgBot::Message::Ptr message) {
        LogInfo() << "Received command " << telegram::commands::kImage << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            ProcessOnDemandCmd(id);
        }
    });
    bot_->getEvents().onCommand(telegram::commands::kPing, [&](TgBot::Message::Ptr message) {
        LogInfo() << "Received command " << telegram::commands::kPing << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            ProcessPingCmd(id);
        }
    });
    bot_->getEvents().onCommand(telegram::commands::kVideos, [&](TgBot::Message::Ptr message) {
        LogInfo() << "Received command " << telegram::commands::kVideos << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            const auto filter = GetFilter(message->text);
            ProcessVideosCmd(id, filter);
        }
    });
    bot_->getEvents().onCommand(telegram::commands::kPreviews, [&](TgBot::Message::Ptr message) {
        LogInfo() << "Received command " << telegram::commands::kPreviews << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            const auto filter = GetFilter(message->text);
            ProcessPreviewsCmd(id, filter);
        }
    });
    bot_->getEvents().onCommand(telegram::commands::kLog, [&](TgBot::Message::Ptr message) {
        LogInfo() << "Received command " << telegram::commands::kLog << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            ProcessLogCmd(id);
        }
    });
    bot_->getEvents().onAnyMessage([&](TgBot::Message::Ptr message) {
        LogInfo() << "Received message " << message->text << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            if (StringTools::startsWith(message->text, telegram::commands::VideoCmdPrefix())) {
                LogInfo() << "video command received: " << message->text;
                const std::string uid =
                    message->text.substr(telegram::commands::VideoCmdPrefix().size());  // uid of file
                ProcessVideoCmd(id, uid);
            }
        } else {
            LogWarning() << "Unauthorized user tried to access: " << id;
        }
    });
    bot_->getEvents().onCallbackQuery([&](TgBot::CallbackQuery::Ptr query) {
        LogInfo() << "Received callback query " << query->message->text << " from user " << query->message->chat->id
                  << " @ " << GetDateTime(query->message);
        if (const auto id = query->message->chat->id; IsUserAllowed(id)) {
            const auto command = query->data.substr(1);  // Remove slash
            if (StringTools::startsWith(query->data, telegram::commands::VideoCmdPrefix())) {
                const std::string video_id = query->data.substr(telegram::commands::VideoCmdPrefix().size());
                ProcessVideoCmd(id, video_id);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(
                           command, telegram::commands::kPreviews)) {  // Space is a separator between cmd and filter
                const auto filter = GetFilter(command.substr(telegram::commands::kPreviews.size()));
                ProcessPreviewsCmd(id, filter);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(
                           command, telegram::commands::kVideos)) {  // Space is a separator between cmd and filter
                const auto filter = GetFilter(command.substr(telegram::commands::kVideos.size()));
                ProcessVideosCmd(id, filter);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, telegram::commands::kImage)) {
                ProcessOnDemandCmd(id);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, telegram::commands::kPing)) {
                ProcessPingCmd(id);
                PostAnswerCallback(query->id);
            }
        }
    });
}

BotFacade::~BotFacade() {
    Stop();
}

void BotFacade::ProcessOnDemandCmd(uint64_t user_id) {
    std::lock_guard lock(photo_mutex_);
    users_waiting_for_photo_.insert(user_id);
}

void BotFacade::ProcessPingCmd(uint64_t user_id) {
    PostTextMessage(PrepareStatusInfo(storage_path_), user_id);
}

void BotFacade::ProcessVideosCmd(uint64_t user_id, const std::optional<Filter>& filter) {
    const auto files = CollectVideoFileUids(storage_path_, filter);
    if (files.empty()) {
        PostTextMessage(translation::messages::kNoFilesFound, user_id);
        return;
    }

    std::string commands_message;
    for (const auto& file : files) {
        std::string command =
            telegram::commands::VideoCmdPrefix() + file.uid + " (" + std::to_string(file.size_mb) + " MB)\n";
        if (commands_message.size() + command.size() > kMaxMessageLen) {
            PostTextMessage(commands_message, user_id);
            commands_message = command;
        } else {
            commands_message += command;
        }
    }
    PostTextMessage(commands_message, user_id);
}

void BotFacade::ProcessPreviewsCmd(uint64_t user_id, const std::optional<Filter>& filter) {
    const auto files = CollectVideoFileUids(storage_path_, filter);
    if (files.empty()) {
        PostTextMessage(translation::messages::kNoFilesFound, user_id);
        return;
    }

    for (const auto& file : files) {
        const std::filesystem::path file_path = storage_path_ / VideoWriter::GeneratePreviewFileName(file.uid);
        PostVideoPreview(file_path, user_id);
    }
    PostTextMessage(translation::messages::kPreviewsSendEnded, user_id);
}

void BotFacade::ProcessVideoCmd(uint64_t user_id, const std::string& video_uid) {
    if (!IsUidValid(video_uid)) {
        LogWarning() << "User " << user_id << " asked file with invalid uid: " << video_uid;
        PostTextMessage(translation::messages::kInvalidFileRequested, user_id);
        return;
    }

    const std::filesystem::path file_path = storage_path_ / VideoWriter::GenerateVideoFileName(video_uid);
    LogInfo() << "File uid: " << video_uid << ", full path: " << file_path;

    if (std::filesystem::exists(file_path)) {
        PostVideo(file_path, user_id);
    } else {
        PostTextMessage(translation::messages::kFileNotFound, user_id);
    }
}

void BotFacade::ProcessLogCmd(uint64_t user_id) {
    const auto log_lines = AppLogTail->dump();
    if (log_lines.empty())
        return;

    std::string message;
    for (const auto& line : log_lines) {
        if (message.size() + line.size() > kMaxMessageLen) {
            PostTextMessage(message, user_id);
            message = line;
        } else {
            message += line;
        }
    }

    PostTextMessage(message, user_id);
}

bool BotFacade::IsUserAllowed(uint64_t user_id) const {
    const auto it = std::find(cbegin(allowed_users_), cend(allowed_users_), user_id);
    if (it == cend(allowed_users_)) {
        LogWarning() << "Unauthorized user access: " << user_id;
        return false;
    }
    return true;
}

bool BotFacade::SomeoneIsWaitingForPhoto() const {
    std::lock_guard lock(photo_mutex_);
    return !users_waiting_for_photo_.empty();
}

void BotFacade::PostOnDemandPhoto(const std::filesystem::path& file_path) {
    {
        std::set<uint64_t> recipients;
        {
            std::lock_guard lock(photo_mutex_);
            std::swap(recipients, users_waiting_for_photo_);
        }

        std::lock_guard lock(queue_mutex_);
        messages_queue_.push_back(telegram::messages::OnDemandPhoto{std::move(recipients), file_path});
    }
    queue_cv_.notify_one();
}

void BotFacade::PostAlarmPhoto(const std::filesystem::path& file_path, const std::string& classes_detected) {
    {
        std::lock_guard lock(queue_mutex_);
        messages_queue_.push_back(telegram::messages::AlarmPhoto{allowed_users_, file_path, classes_detected});
    }
    queue_cv_.notify_one();
}

void BotFacade::PostTextMessage(const std::string& message, const std::optional<uint64_t>& user_id) {
    {
        std::set<uint64_t> recipients = (user_id ? std::set<uint64_t>{*user_id} : allowed_users_);
        std::lock_guard lock(queue_mutex_);
        messages_queue_.push_back(telegram::messages::TextMessage{std::move(recipients), message});
    }
    queue_cv_.notify_one();
}

void BotFacade::PostVideoPreview(const std::filesystem::path& file_path, const std::optional<uint64_t>& user_id) {
    {
        std::set<uint64_t> recipients = (user_id ? std::set<uint64_t>{*user_id} : allowed_users_);
        std::lock_guard lock(queue_mutex_);
        messages_queue_.push_back(telegram::messages::Preview{std::move(recipients), file_path});
    }
    queue_cv_.notify_one();
}

void BotFacade::PostVideo(const std::filesystem::path& file_path, const std::optional<uint64_t>& user_id) {
    {
        std::set<uint64_t> recipients = (user_id ? std::set<uint64_t>{*user_id} : allowed_users_);
        std::lock_guard lock(queue_mutex_);
        messages_queue_.push_back(telegram::messages::Video{std::move(recipients), file_path});
    }
    queue_cv_.notify_one();
}

void BotFacade::PostMenu(uint64_t user_id) {
    {
        std::lock_guard lock(queue_mutex_);
        messages_queue_.push_back(telegram::messages::Menu{user_id});
    }
    queue_cv_.notify_one();
}

void BotFacade::PostAnswerCallback(const std::string& callback_id) {
    {
        std::lock_guard lock(queue_mutex_);
        messages_queue_.push_back(telegram::messages::Answer{callback_id});
    }
    queue_cv_.notify_one();
}

void BotFacade::PollThreadFunc() {
    try {
        if (!bot_->getApi().deleteWebhook())
            LOG_ERROR << "Unable to delete bot Webhook";
    } catch (std::exception& e) {
        LOG_EXCEPTION("Exception while prepare bot polling", e);
    }

    TgBot::TgLongPoll long_poll(*bot_);

    while (!stop_) {
        try {
            LogTrace() << "LongPoll start";
            long_poll.start();
        } catch (std::exception& e) {
            LOG_EXCEPTION("Exception while start polling", e);
        }
    }
}

void BotFacade::QueueThreadFunc() {
    while (!stop_) {
        std::unique_lock lock(queue_mutex_);
        queue_cv_.wait(lock, [&] { return !messages_queue_.empty() || stop_; });
        if (stop_)
            break;

        const auto message = messages_queue_.front();
        messages_queue_.pop_front();
        lock.unlock();

        std::visit(message_sender_, message);
    }
}

void BotFacade::Start() {
    if (!stop_) {
        LogInfo() << "Attempt start() on already running bot";
        return;
    }

    stop_ = false;
    poll_thread_ = std::jthread(&BotFacade::PollThreadFunc, this);
    queue_thread_ = std::jthread(&BotFacade::QueueThreadFunc, this);
}

void BotFacade::Stop() {
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

}  // namespace telegram
