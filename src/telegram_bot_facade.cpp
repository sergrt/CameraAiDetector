#include "telegram_bot_facade.h"

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

constexpr size_t kMaxMessageLen{4096};
constexpr std::chrono::minutes kDefaultPauseTime{60};

extern SafePtr<RingBuffer<std::string>> AppLogTail;
extern std::chrono::time_point<std::chrono::steady_clock> kStartTime;

namespace telegram {

namespace {

std::optional<std::chrono::minutes> GetParameterTimeMin(const std::string& text) {
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
            return std::nullopt;
        }

        std::chrono::minutes res{};
        if (period == "M") {
            res = std::chrono::minutes(count);
        } else if (period == "H") {
            res = std::chrono::minutes(count * 60);
        } else if (period == "D") {
            res = std::chrono::minutes(count * 60 * 24);
        } else {
            throw std::runtime_error("Unhandled time period in parameter");
        }

        return std::optional<std::chrono::minutes>(res);
    }

    return std::nullopt;
}

std::optional<Filter> GetFilter(const std::string& text) {
    return std::optional<Filter>{GetParameterTimeMin(text)};
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

BotFacade::BotFacade(const std::string& token, std::filesystem::path storage_path, std::set<uint64_t> allowed_users, std::set<uint64_t> admin_users)
    : bot_{std::make_unique<TgBot::Bot>(token)}
    , message_sender_{bot_.get(), storage_path}
    , storage_path_{std::move(storage_path)}
    , allowed_users_{std::move(allowed_users)}
    , admin_users_{std::move(admin_users)} {

    SetupBotCommands();
}

void BotFacade::SetupBotCommands() {
    bot_->getEvents().onCommand(telegram::commands::kStart, [this](TgBot::Message::Ptr message) {
        LOG_INFO << "Received command " << telegram::commands::kStart << " from user " << message->chat->id;
        const auto id = message->chat->id;
        if (IsUserAdmin(id)) {
            PostAdminMenu(id);
        } else if (IsUserAllowed(id)) {
            PostMenu(id);
        }
    });
    bot_->getEvents().onCommand(telegram::commands::kImage, [this](TgBot::Message::Ptr message) {
        LOG_INFO << "Received command " << telegram::commands::kImage << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            ProcessOnDemandCmd(id);
        }
    });
    bot_->getEvents().onCommand(telegram::commands::kPing, [this](TgBot::Message::Ptr message) {
        LOG_INFO << "Received command " << telegram::commands::kPing << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            ProcessStatusCmd(id);
        }
    });
    bot_->getEvents().onCommand(telegram::commands::kVideos, [this](TgBot::Message::Ptr message) {
        LOG_INFO << "Received command " << telegram::commands::kVideos << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            const auto filter = GetFilter(message->text);
            ProcessVideosCmd(id, filter);
        }
    });
    bot_->getEvents().onCommand(telegram::commands::kPreviews, [this](TgBot::Message::Ptr message) {
        LOG_INFO << "Received command " << telegram::commands::kPreviews << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            const auto filter = GetFilter(message->text);
            ProcessPreviewsCmd(id, filter);
        }
    });
    bot_->getEvents().onCommand(telegram::commands::kLog, [this](TgBot::Message::Ptr message) {
        LOG_INFO << "Received command " << telegram::commands::kLog << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAdmin(id)) {
            ProcessLogCmd(id);
        }
    });
    bot_->getEvents().onCommand(telegram::commands::kPause, [this](TgBot::Message::Ptr message) {
        LOG_INFO << "Received command " << telegram::commands::kPause << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            auto pause_time = GetParameterTimeMin(message->text);
            if (!pause_time)
                *pause_time = kDefaultPauseTime;
            ProcessPauseCmd(id, *pause_time);
        }
    });
    bot_->getEvents().onCommand(telegram::commands::kResume, [this](TgBot::Message::Ptr message) {
        LOG_INFO << "Received command " << telegram::commands::kPause << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            ProcessResumeCmd(id);
        }
    });
    bot_->getEvents().onAnyMessage([this](TgBot::Message::Ptr message) {
        LOG_INFO << "Received message " << message->text << " from user " << message->chat->id;
        if (const auto id = message->chat->id; IsUserAllowed(id)) {
            if (StringTools::startsWith(message->text, telegram::commands::VideoCmdPrefix())) {
                LOG_INFO << "video command received: " << message->text;
                const std::string uid = message->text.substr(telegram::commands::VideoCmdPrefix().size());  // uid of file
                ProcessVideoCmd(id, uid);
            }
        } else {
            LOG_WARNING << "Unauthorized user tried to access: " << id;
        }
    });
    bot_->getEvents().onCallbackQuery([this](TgBot::CallbackQuery::Ptr query) {
        LOG_INFO << "Received callback query " << query->message->text << " from user " << query->message->chat->id
                 << " @ " << GetDateTime(query->message);
        if (const auto id = query->message->chat->id; IsUserAllowed(id)) {
            const auto command = query->data.substr(1);  // Remove slash
            if (StringTools::startsWith(query->data, telegram::commands::VideoCmdPrefix())) {
                const std::string video_id = query->data.substr(telegram::commands::VideoCmdPrefix().size());
                ProcessVideoCmd(id, video_id);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, telegram::commands::kPreviews)) {  // Space is a separator between cmd and filter
                const auto filter = GetFilter(command.substr(telegram::commands::kPreviews.size()));
                ProcessPreviewsCmd(id, filter);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, telegram::commands::kVideos)) {  // Space is a separator between cmd and filter
                const auto filter = GetFilter(command.substr(telegram::commands::kVideos.size()));
                ProcessVideosCmd(id, filter);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, telegram::commands::kImage)) {
                ProcessOnDemandCmd(id);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, telegram::commands::kPing)) {
                ProcessStatusCmd(id);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, telegram::commands::kPause)) {
                //TODO: Pause and Resume commands use the same code as bot commands, consider to refactor
                auto pause_min = GetParameterTimeMin(command.substr(telegram::commands::kPause.size()));
                if (!pause_min)
                    *pause_min = kDefaultPauseTime;
                 ProcessPauseCmd(id, *pause_min);
                 PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, telegram::commands::kResume)) {
                ProcessResumeCmd(id);
                PostAnswerCallback(query->id);
            } else if (StringTools::startsWith(command, telegram::commands::kLog)) {
                ProcessLogCmd(id);
                PostAnswerCallback(query->id);
            }
        }
    });
}

BotFacade::~BotFacade() {
    Stop();
}

std::string BotFacade::PrepareStatusInfo(uint64_t requested_by) {
    const auto cur_time = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::system_clock::now()};
    std::string timestamp = std::format("{:%d-%m-%Y %H:%M:%S}", cur_time);
    timestamp.erase(begin(timestamp) + timestamp.find('.'), end(timestamp));

    std::string free_space = std::to_string(std::filesystem::space(storage_path_).available / 1'000'000);
    for (auto i = static_cast<int>(free_space.size()) - 3; i > 0; i -= 3) {
        free_space.insert(i, "'");
    }
    // Some useful utf chars: &#9989; &#127909; &#128247; &#128680; &#128226; &#128266; &#10071; &#128681; &#8505;
    // &#127916; &#127910; &#128064;
    auto message = "&#8505; " + timestamp + ",\n" + free_space + " MB " + translation::messages::kAvailable + ",\n" +
           GetUptime() + " " + translation::messages::kUptime;

    UpdatePausedUsers();
    if (auto it = paused_users_.find(requested_by); it != paused_users_.end()) {
        message += std::string{",\n"} + translation::messages::kNotificationsPaused + " " + GetDateTimeString(it->second);
    }

    return message;
}

void BotFacade::ProcessOnDemandCmd(uint64_t user_id) {
    std::lock_guard lock(photo_mutex_);
    users_waiting_for_photo_.insert(user_id);
}

void BotFacade::ProcessStatusCmd(uint64_t user_id) {
    PostStatusMessage(PrepareStatusInfo(user_id), user_id);
}

void BotFacade::ProcessVideosCmd(uint64_t user_id, const std::optional<Filter>& filter) {
    const auto files = CollectVideoFileUids(storage_path_, filter);
    if (files.empty()) {
        PostTextMessage(translation::messages::kNoFilesFound, user_id);
        return;
    }

    std::string commands_message;
    for (const auto& file : files) {
        std::string command = telegram::commands::VideoCmdPrefix() + file.uid + " (" + std::to_string(file.size_mb) + " MB)\n";
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
        LOG_WARNING << "User " << user_id << " asked file with invalid uid: " << video_uid;
        PostTextMessage(translation::messages::kInvalidFileRequested, user_id);
        return;
    }

    const std::filesystem::path file_path = storage_path_ / VideoWriter::GenerateVideoFileName(video_uid);
    LOG_INFO << "File uid: " << video_uid << ", full path: " << file_path;

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

void BotFacade::ProcessPauseCmd(uint64_t user_id, std::chrono::minutes pause_time) {
    const auto end_time = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::system_clock::now() + pause_time};
    PostTextMessage(translation::messages::kNotificationsPaused + " " + GetDateTimeString(end_time), user_id);
    paused_users_[user_id] = end_time;
}

void BotFacade::ProcessResumeCmd(uint64_t user_id) {
    RemoveUserFromPaused(user_id);
    PostTextMessage(translation::messages::kNotificationsResumed, user_id);
}

bool BotFacade::IsUserAllowed(uint64_t user_id) const {
    const auto it = std::find(cbegin(allowed_users_), cend(allowed_users_), user_id);
    if (it == cend(allowed_users_)) {
        LOG_WARNING << "Unauthorized user access: " << user_id;
        return false;
    }
    return true;
}

bool BotFacade::IsUserAdmin(uint64_t user_id) const {
    const auto it = std::find(cbegin(admin_users_), cend(admin_users_), user_id);
    if (it == cend(admin_users_)) {
        LOG_WARNING << "Unauthorized admin user access: " << user_id;
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

        // Do not filter users here: users explicitly request this image

        std::lock_guard lock(queue_mutex_);
        messages_queue_.push_back(telegram::messages::OnDemandPhoto{std::move(recipients), file_path});
    }
    queue_cv_.notify_one();
}

void BotFacade::PostAlarmPhoto(const std::filesystem::path& file_path, const std::string& classes_detected) {
    std::set<uint64_t> recipients = UpdateGetUnpausedRecipients(allowed_users_);
    if (recipients.empty())
        return;

    {
        std::lock_guard lock(queue_mutex_);
        messages_queue_.push_back(telegram::messages::AlarmPhoto{std::move(recipients), file_path, classes_detected});
    }
    queue_cv_.notify_one();
}

void BotFacade::PostStatusMessage(const std::string& message, uint64_t user_id) {
    // Explicit message - do not check
    std::set<uint64_t> recipients = std::set<uint64_t>{user_id};
    {
        std::lock_guard lock(queue_mutex_);
        messages_queue_.push_back(telegram::messages::TextMessage{std::move(recipients), message});
    }
    queue_cv_.notify_one();
}

void BotFacade::PostTextMessage(const std::string& message, const std::optional<uint64_t>& user_id) {
    std::set<uint64_t> recipients = UpdateGetUnpausedRecipients(user_id ? std::set<uint64_t>{*user_id} : allowed_users_, user_id);
    if (recipients.empty())
        return;

    {
        std::lock_guard lock(queue_mutex_);
        messages_queue_.push_back(telegram::messages::TextMessage{std::move(recipients), message});
    }
    queue_cv_.notify_one();
}

void BotFacade::PostVideoPreview(const std::filesystem::path& file_path, const std::optional<uint64_t>& user_id) {
    std::set<uint64_t> recipients = UpdateGetUnpausedRecipients(user_id ? std::set<uint64_t>{*user_id} : allowed_users_, user_id);
    if (recipients.empty())
        return;

    {
        std::lock_guard lock(queue_mutex_);
        messages_queue_.push_back(telegram::messages::Preview{std::move(recipients), file_path});
    }
    queue_cv_.notify_one();
}

void BotFacade::PostVideo(const std::filesystem::path& file_path, const std::optional<uint64_t>& user_id) {
    std::set<uint64_t> recipients = UpdateGetUnpausedRecipients(user_id ? std::set<uint64_t>{*user_id} : allowed_users_, user_id);
    if (recipients.empty())
        return;

    {
        std::lock_guard lock(queue_mutex_);
        messages_queue_.push_back(telegram::messages::Video{std::move(recipients), file_path});
    }
    queue_cv_.notify_one();
}

void BotFacade::PostMenu(uint64_t user_id) {
    // Do not check for paused user
    {
        std::lock_guard lock(queue_mutex_);
        messages_queue_.push_back(telegram::messages::Menu{user_id});
    }
    queue_cv_.notify_one();
}

void BotFacade::PostAdminMenu(uint64_t user_id) {
    // Do not check for paused user
    {
        std::lock_guard lock(queue_mutex_);
        messages_queue_.push_back(telegram::messages::AdminMenu{user_id});
    }
    queue_cv_.notify_one();
}

void BotFacade::PostAnswerCallback(const std::string& callback_id) {
    // Do not check for paused user
    {
        std::lock_guard lock(queue_mutex_);
        messages_queue_.push_back(telegram::messages::Answer{callback_id});
    }
    queue_cv_.notify_one();
}

void BotFacade::UpdatePausedUsers() {
    const auto cur_time = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::system_clock::now()}.get_local_time();
    std::erase_if(paused_users_, [&cur_time](const auto& p) {
        return p.second.get_local_time() <= cur_time;
    });
}

void BotFacade::RemoveUserFromPaused(uint64_t user_id) {
    std::erase_if(paused_users_, [user_id](const auto& p) {
        return p.first == user_id;
    });
}

std::set<uint64_t> BotFacade::UpdateGetUnpausedRecipients(const std::set<uint64_t>& users, std::optional<uint64_t> requester/* = std::nullopt*/) {
    // TODO: consider add some delay, to update maps less often to be easier on resources
    UpdatePausedUsers();

    auto res = users;
    for (const auto& p : paused_users_) {
        if (auto it = res.find(p.first); it != res.end()) {
            res.erase(it);
        }
    }

    if (requester) { // Explicitly requested
        res.insert(*requester);
    }
    return res;
}

void BotFacade::PollThreadFunc(std::stop_token stop_token) {
    try {
        if (!bot_->getApi().deleteWebhook())
            LOG_ERROR_EX << "Unable to delete bot Webhook";
    } catch (std::exception& e) {
        LOG_EXCEPTION("Exception while prepare bot polling", e);
    }

    TgBot::TgLongPoll long_poll(*bot_);

    while (!stop_token.stop_requested()) {
        try {
            LOG_TRACE << "LongPoll start";
            long_poll.start();
        } catch (std::exception& e) {
            LOG_EXCEPTION("Exception while start polling", e);
        }
    }
}

void BotFacade::QueueThreadFunc(std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        std::unique_lock lock(queue_mutex_);
        queue_cv_.wait(lock, [&] { return !messages_queue_.empty() || stop_token.stop_requested(); });
        if (stop_token.stop_requested())
            break;

        const auto message = messages_queue_.front();
        messages_queue_.pop_front();
        lock.unlock();

        std::visit(message_sender_, message);
    }
}

void BotFacade::Start() {
    if (poll_thread_.joinable() || queue_thread_.joinable()) {
        LOG_INFO << "Attempt start() on already running bot";
        return;
    }

    poll_thread_ = std::jthread(std::bind_front(&BotFacade::PollThreadFunc, this));
    queue_thread_ = std::jthread(std::bind_front(&BotFacade::QueueThreadFunc, this));
}

void BotFacade::Stop() {
    const auto poll_stop_requested = poll_thread_.request_stop();
    const auto queue_stop_requested = queue_thread_.request_stop();

    if (!poll_stop_requested || !queue_stop_requested) {
        LOG_INFO << "Attempt stop() on already stopped bot. Poll stop request result = " << poll_stop_requested
                 << ", queue stop request result = " << queue_stop_requested;
    }

    queue_cv_.notify_all();

    // In case this function is not called from within destructor, ensure that threads are stopped
    if (poll_thread_.joinable())
        poll_thread_.join();

    if (queue_thread_.joinable())
        queue_thread_.join();
}

}  // namespace telegram
