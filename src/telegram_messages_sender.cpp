#include "telegram_messages_sender.h"

#include "helpers.h"
#include "log.h"
#include "translation.h"
#include "uid_utils.h"
#include "video_utils.h"
#include "video_writer.h"

#include <filesystem>

namespace {

TgBot::InlineKeyboardMarkup::Ptr MakeStartMenu() {
    using namespace translation::menu;
    auto keyboard = std::make_shared<TgBot::InlineKeyboardMarkup>();
    {
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kViews + " 1" + kHour;
        row.back()->callbackData = "/" + telegram::commands::kPreviews + " 1h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kViews + " 12" + kHour;
        row.back()->callbackData = "/" + telegram::commands::kPreviews + " 12h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kViews + " 24" + kHour;
        row.back()->callbackData = "/" + telegram::commands::kPreviews + " 24h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kViews + " " + kAll;
        row.back()->callbackData = "/" + telegram::commands::kPreviews;
        keyboard->inlineKeyboard.push_back(std::move(row));
    }
    {
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kVideos + " 1" + kHour;
        row.back()->callbackData = "/" + telegram::commands::kVideos + " 1h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kVideos + " 12" + kHour;
        row.back()->callbackData = "/" + telegram::commands::kVideos + " 12h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kVideos + " 24" + kHour;
        row.back()->callbackData = "/" + telegram::commands::kVideos + " 24h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kVideos + " " + kAll;
        row.back()->callbackData = "/" + telegram::commands::kVideos;
        keyboard->inlineKeyboard.push_back(std::move(row));
    }
    {
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kPause + " 1" + kHour;
        row.back()->callbackData = "/" + telegram::commands::kPause + " 1h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kPause + " 12" + kHour;
        row.back()->callbackData = "/" + telegram::commands::kPause + " 12h";
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kResume;
        row.back()->callbackData = "/" + telegram::commands::kResume;
        keyboard->inlineKeyboard.push_back(std::move(row));
    }
    {
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kImage;
        row.back()->callbackData = "/" + telegram::commands::kImage;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kPing;
        row.back()->callbackData = "/" + telegram::commands::kPing;
        keyboard->inlineKeyboard.push_back(std::move(row));
    }
    return keyboard;
}

TgBot::InlineKeyboardMarkup::Ptr MakeAdminStartMenu() {
    using namespace translation::menu;

    auto keyboard = MakeStartMenu();
    {
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        row.emplace_back(new TgBot::InlineKeyboardButton());
        row.back()->text = kLog;
        row.back()->callbackData = "/" + telegram::commands::kLog;
        keyboard->inlineKeyboard.push_back(std::move(row));
    }
    return keyboard;
}

std::string updateCaption(const std::string& caption, int retryNumber) {
    if (retryNumber > 0) {
        return caption + " (retry " + std::to_string(retryNumber) + ")";
    }
    return caption;
}

}  // namespace

namespace telegram {

MessagesSender::MessagesSender(TgBot::Bot* bot, std::filesystem::path storage_path)
    : bot_{bot}
    , storage_path_{std::move(storage_path)}
    , start_menu_{MakeStartMenu()}
    , admin_start_menu_{MakeAdminStartMenu()}
    , resend_thread_{&MessagesSender::ResendFn, this} {
    if (!bot_) {
        static const auto err_msg = "Invalid tg bot dependency";
        LOG_ERROR_EX << err_msg;
        throw std::runtime_error(err_msg);
    }
}

MessagesSender::~MessagesSender() {
    const auto resend_stop_requested = resend_thread_.request_stop();
    if (!resend_stop_requested)
        LOG_ERROR << "Resend thread stop request failed";

    if (resend_thread_.joinable())
        resend_thread_.join();
}

void MessagesSender::ResendFn(std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        std::this_thread::sleep_for(std::chrono::seconds(15));

        if (stop_token.stop_requested())
            break;

        std::lock_guard lock(resend_mutex_);
        while (!resend_queue_.empty()) {
            const auto& fn = resend_queue_.front();
            bool send_result = false;
            try {
                LOG_DEBUG << "Resending message from resend queue";
                send_result = fn();
                if (send_result) {
                    LOG_DEBUG << "Resend succeeded";
                }
            } catch (std::exception& e) {
                LOG_EXCEPTION("Exception while sending message from resend queue", e);
            }

            if (!send_result) {
                LOG_DEBUG << "Resend failed";
                break;
            }
            resend_queue_.pop_front();
        }
    }
}

void MessagesSender::operator()(const telegram::messages::TextMessage& message) {
    std::vector<std::function<bool()>> resend_pack;
    for (const auto& user : message.recipients) {
        auto fn = [bot = bot_, user, text = message.text, retryNo = 0]() mutable -> bool {
            const auto textWithInfo = updateCaption(text, retryNo);
            ++retryNo;
            return bot->getApi().sendMessage(user, textWithInfo, nullptr, nullptr, nullptr, "HTML") != nullptr;
        };

        bool send_result = false;
        try {
            send_result = fn();
            if (!send_result)
                LOG_ERROR_EX << "Message send failed to user " << user;
        } catch (std::exception& e) {
            LOG_EXCEPTION("Exception while sending message", e);
        }

        if (!send_result) {
            resend_pack.push_back(std::move(fn));
        }
    }

    if (!resend_pack.empty()) {
        std::lock_guard lock(resend_mutex_);
        resend_queue_.insert(resend_queue_.end(), std::make_move_iterator(std::begin(resend_pack)), std::make_move_iterator(std::end(resend_pack)));
    }
}

void MessagesSender::operator()(const telegram::messages::OnDemandPhoto& message) {
    const auto& file_path = message.file_path;
    LOG_DEBUG << "Sending on-demand photo to " << LOG_VAR(message.recipients.size()) << " users: " << file_path;
    if (!std::filesystem::exists(file_path)) {
        LOG_ERROR_EX << "On-demand photo file is missing: " << file_path;
        return;
    }

    const auto photo = TgBot::InputFile::fromFile(file_path.generic_string(), "image/jpeg");
    const auto caption = "&#128064; " + GetHumanDateTime(file_path.filename().generic_string());  // &#128064; - eyes

    std::vector<std::function<bool()>> resend_pack;
    for (const auto& user : message.recipients) {
        auto fn = [bot = bot_, user, photo, caption = caption, retryNo = 0]() mutable -> bool {
            const auto captionWithInfo = updateCaption(caption, retryNo);
            ++retryNo;
            return bot->getApi().sendPhoto(user, photo, captionWithInfo, 0, nullptr, "HTML") != nullptr;
        };

        bool send_result = false;
        try {
            send_result = fn();
            if (!send_result)
                LOG_ERROR_EX << "On-demand photo send failed to user " << user;
        } catch (std::exception& e) {
            LOG_EXCEPTION("Exception while sending photo", e);
        }

        if (!send_result) {
            LOG_DEBUG << "Add to resend queue";
            resend_pack.push_back(std::move(fn));
        }
    }

    if (!resend_pack.empty()) {
        std::lock_guard lock(resend_mutex_);
        resend_queue_.insert(resend_queue_.end(), std::make_move_iterator(std::begin(resend_pack)), std::make_move_iterator(std::end(resend_pack)));
    }
}

void MessagesSender::operator()(const telegram::messages::AlarmPhoto& message) {
    const auto& file_path = message.file_path;

    if (!std::filesystem::exists(file_path)) {
        LOG_ERROR_EX << "Alarm photo file is missing: " << file_path;
        return;
    }

    const auto photo = TgBot::InputFile::fromFile(file_path.generic_string(), "image/jpeg");
    const auto caption = "&#10071; " + GetHumanDateTime(file_path.filename().generic_string())  // &#10071; - red exclamation mark
                         + (message.detections.empty() ? "" : " (" + message.detections + ")");

    std::vector<std::function<bool()>> resend_pack;
    for (const auto& user : message.recipients) {
        auto fn = [bot = bot_, user, photo, caption = caption, retryNo = 0]() mutable -> bool {
            const auto captionWithInfo = updateCaption(caption, retryNo);
            ++retryNo;
            return bot->getApi().sendPhoto(user, photo, captionWithInfo, 0, nullptr, "HTML") != nullptr;
        };

        bool send_result = false;
        try {
            send_result = fn();
            if (!send_result)
                LOG_ERROR_EX << "Alarm photo send failed to user " << user;
        } catch (std::exception& e) {
            LOG_EXCEPTION("Exception while sending photo", e);
        }

        if (!send_result) {
            resend_pack.push_back(std::move(fn));
        }
    }

    if (!resend_pack.empty()) {
        std::lock_guard lock(resend_mutex_);
        resend_queue_.insert(resend_queue_.end(), std::make_move_iterator(std::begin(resend_pack)), std::make_move_iterator(std::end(resend_pack)));
    }
}

void MessagesSender::operator()(const telegram::messages::Preview& message) {
    const auto& file_path = message.file_path;

    if (!std::filesystem::exists(file_path)) {
        LOG_ERROR_EX << "Preview file is missing: " << file_path;
        return;
    }

    const auto file_name = file_path.filename().generic_string();
    const auto uid = GetUidFromFileName(file_name);
    const auto video_file_path = storage_path_ / VideoWriter::GenerateVideoFileName(uid);

    if (!std::filesystem::exists(video_file_path)) {
        LOG_ERROR_EX << "Video file is missing: " << LOG_VAR(uid);
        return;
    }

    const auto photo = TgBot::InputFile::fromFile(file_path.generic_string(), "image/jpeg");
    const auto cmd = telegram::commands::VideoCmdPrefix() + uid;

    auto keyboard = std::make_shared<TgBot::InlineKeyboardMarkup>();
    auto view_button = std::make_shared<TgBot::InlineKeyboardButton>();

    view_button->text = GetHumanDateTime(file_name) + " (" + std::to_string(GetFileSizeMb(video_file_path)) + " MB)";
    view_button->callbackData = cmd;
    keyboard->inlineKeyboard.push_back({view_button});

    std::vector<std::function<bool()>> resend_pack;
    for (const auto& user : message.recipients) {
        auto fn = [bot = bot_, user, photo, keyboard, retryNo = 0]() mutable -> bool {
            const auto captionWithInfo = updateCaption("", retryNo);
            ++retryNo;
            return bot->getApi().sendPhoto(user, photo, captionWithInfo, 0, keyboard, "", true) != nullptr;  // NOTE: No notification here
        };

        bool send_result = false;
        try {
            send_result = fn();
            if (!send_result)
                LOG_ERROR_EX << "Video preview send failed to user " << user;
        } catch (std::exception& e) {
            LOG_EXCEPTION("Exception while sending preview", e);
        }

        if (!send_result) {
            resend_pack.push_back(std::move(fn));
        }
    }

    if (!resend_pack.empty()) {
        std::lock_guard lock(resend_mutex_);
        resend_queue_.insert(resend_queue_.end(), std::make_move_iterator(std::begin(resend_pack)), std::make_move_iterator(std::end(resend_pack)));
    }
}

void MessagesSender::operator()(const telegram::messages::Video& message) {
    const auto& file_path = message.file_path;

    if (!std::filesystem::exists(file_path)) {
        LOG_ERROR_EX << "Video file is missing: " << file_path;
        return;
    }

    auto splitted_files = GetSplittedFileNames(file_path);
    const size_t total_parts = splitted_files.size();
    size_t part_number = 0;
    std::vector<std::function<bool()>> resend_pack;

    for (const auto& part_file_path : splitted_files) {
        ++part_number;
        for (const auto& user : message.recipients) {
            const auto video = TgBot::InputFile::fromFile(part_file_path.generic_string(), "video/mp4");
            auto caption = "&#127910; " + GetHumanDateTime(file_path.filename().generic_string());  // &#127910; - video camera
            if (total_parts > 1) {
                caption += " (" + std::to_string(part_number) + "/" + std::to_string(total_parts) + ")";
            }

            auto fn = [bot = bot_, user, video, caption = caption, retryNo = 0]() mutable -> bool {
                const auto captionWithInfo = updateCaption(caption, retryNo);
                ++retryNo;
                return bot->getApi().sendVideo(user, video, false, 0, 0, 0, "", captionWithInfo, 0, nullptr, "HTML") != nullptr;
            };

            bool send_result = false;
            try {
                send_result = fn();
                if (!send_result)
                    LOG_ERROR_EX << "Video file " << file_path << " send failed to user " << user;
            } catch (std::exception& e) {
                LOG_EXCEPTION("Exception while sending video", e);
            }

            if (!send_result) {
                resend_pack.push_back(std::move(fn));
            }
        }
    }

    if (!resend_pack.empty()) {
        std::lock_guard lock(resend_mutex_);
        resend_queue_.insert(resend_queue_.end(), std::make_move_iterator(std::begin(resend_pack)), std::make_move_iterator(std::end(resend_pack)));
    }
}

void MessagesSender::operator()(const telegram::messages::Menu& message) {
    try {
        if (!bot_->getApi().sendMessage(message.recipient, translation::menu::kCaption, nullptr, nullptr, start_menu_, "HTML"))
            LOG_ERROR_EX << "/start reply send failed to user " << message.recipient;
    } catch (std::exception& e) {
        LOG_EXCEPTION("Exception while sending menu", e);
    }
}

void MessagesSender::operator()(const telegram::messages::AdminMenu& message) {
    try {
        if (!bot_->getApi().sendMessage(message.recipient, translation::menu::kCaption, nullptr, nullptr, admin_start_menu_, "HTML"))
            LOG_ERROR_EX << "/start reply send failed to user " << message.recipient;
    } catch (std::exception& e) {
        LOG_EXCEPTION("Exception while sending admin menu", e);
    }
}

void MessagesSender::operator()(const telegram::messages::Answer& message) {
    try {
        if (!bot_->getApi().answerCallbackQuery(message.callback_id, "", false, "", 20))
            LOG_ERROR_EX << "Answer callback query send failed";
    } catch (std::exception& e) {
        // Timed-out queries trigger this exception, so this exception might be non-fatal, but still logged
        LOG_EXCEPTION("Exception (non-fatal?) while sending answer callback query", e);
    }
}

}  // namespace telegram
