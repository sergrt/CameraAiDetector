#include "telegram_messages_sender.h"

#include "helpers.h"
#include "log.h"
#include "translation.h"
#include "uid_utils.h"
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

}  // namespace

namespace telegram {

MessagesSender::MessagesSender(TgBot::Bot* bot, std::filesystem::path storage_path)
    : bot_{bot}
    , storage_path_{std::move(storage_path)}
    , start_menu_{MakeStartMenu()}
    , admin_start_menu_{MakeAdminStartMenu()} {
    if (!bot_) {
        static const auto err_msg = "Invalid tg bot dependency";
        LOG_ERROR << err_msg;
        throw std::runtime_error(err_msg);
    }
}

void MessagesSender::operator()(const telegram::messages::TextMessage& message) {
    for (const auto& user : message.recipients) {
        try {
            if (!bot_->getApi().sendMessage(user, message.text, nullptr, nullptr, nullptr, "HTML"))
                LOG_ERROR << "Message send failed to user " << user;
        } catch (std::exception& e) {
            LOG_EXCEPTION("Exception while sending message", e);
        }
    }
}

void MessagesSender::operator()(const telegram::messages::OnDemandPhoto& message) {
    const auto& file_path = message.file_path;

    if (!std::filesystem::exists(file_path)) {
        LOG_ERROR << "On-demand photo file is missing: " << file_path;
        return;
    }

    const auto photo = TgBot::InputFile::fromFile(file_path.generic_string(), "image/jpeg");
    const auto caption = "&#128064; " + GetHumanDateTime(file_path.filename().generic_string());  // &#128064; - eyes

    for (const auto& user : message.recipients) {
        try {
            if (!bot_->getApi().sendPhoto(user, photo, caption, 0, nullptr, "HTML"))
                LOG_ERROR << "On-demand photo send failed to user " << user;
        } catch (std::exception& e) {
            LOG_EXCEPTION("Exception while sending photo", e);
        }
    }
}

void MessagesSender::operator()(const telegram::messages::AlarmPhoto& message) {
    const auto& file_path = message.file_path;

    if (!std::filesystem::exists(file_path)) {
        LOG_ERROR << "Alarm photo file is missing: " << file_path;
        return;
    }

    const auto photo = TgBot::InputFile::fromFile(file_path.generic_string(), "image/jpeg");
    const auto caption = "&#10071; " + GetHumanDateTime(file_path.filename().generic_string())  // &#10071; - red exclamation mark
                         + (message.detections.empty() ? "" : " (" + message.detections + ")");

    for (const auto& user : message.recipients) {
        try {
            if (!bot_->getApi().sendPhoto(user, photo, caption, 0, nullptr, "HTML"))
                LOG_ERROR << "Alarm photo send failed to user " << user;
        } catch (std::exception& e) {
            LOG_EXCEPTION("Exception while sending photo", e);
        }
    }
}

void MessagesSender::operator()(const telegram::messages::Preview& message) {
    const auto& file_path = message.file_path;

    if (!std::filesystem::exists(file_path)) {
        LOG_ERROR << "Preview file is missing: " << file_path;
        return;
    }

    const auto file_name = file_path.filename().generic_string();
    const auto uid = GetUidFromFileName(file_name);
    const auto video_file_path = storage_path_ / VideoWriter::GenerateVideoFileName(uid);

    if (!std::filesystem::exists(video_file_path)) {
        LOG_ERROR << "Video file is missing: " << LOG_VAR(uid);
        return;
    }

    const auto photo = TgBot::InputFile::fromFile(file_path.generic_string(), "image/jpeg");
    const auto cmd = telegram::commands::VideoCmdPrefix() + uid;

    auto keyboard = std::make_shared<TgBot::InlineKeyboardMarkup>();
    auto view_button = std::make_shared<TgBot::InlineKeyboardButton>();

    view_button->text = GetHumanDateTime(file_name) + " (" + std::to_string(GetFileSizeMb(video_file_path)) + " MB)";
    view_button->callbackData = cmd;
    keyboard->inlineKeyboard.push_back({view_button});

    for (const auto& user_id : message.recipients) {
        try {
            if (!bot_->getApi().sendPhoto(user_id, photo, "", 0, keyboard, "", true))  // NOTE: No notification here
                LOG_ERROR << "Video preview send failed to user " << user_id;
        } catch (std::exception& e) {
            LOG_EXCEPTION("Exception while sending photo", e);
        }
    }
}

void MessagesSender::operator()(const telegram::messages::Video& message) {
    const auto& file_path = message.file_path;

    if (!std::filesystem::exists(file_path)) {
        LOG_ERROR << "Video file is missing: " << file_path;
        return;
    }

    const auto video = TgBot::InputFile::fromFile(file_path.generic_string(), "video/mp4");
    const auto caption = "&#127910; " + GetHumanDateTime(file_path.filename().generic_string());  // &#127910; - video camera

    for (const auto& user_id : message.recipients) {
        try {
            if (!bot_->getApi().sendVideo(user_id, video, false, 0, 0, 0, "", caption, 0, nullptr, "HTML"))
                LOG_ERROR << "Video file " << file_path << " send failed to user " << user_id;
        } catch (std::exception& e) {
            LOG_EXCEPTION("Exception while sending video", e);
        }
    }
}

void MessagesSender::operator()(const telegram::messages::Menu& message) {
    try {
        if (!bot_->getApi().sendMessage(message.recipient, translation::menu::kCaption, nullptr, nullptr, start_menu_, "HTML"))
            LOG_ERROR << "/start reply send failed to user " << message.recipient;
    } catch (std::exception& e) {
        LOG_EXCEPTION("Exception while sending menu", e);
    }
}

void MessagesSender::operator()(const telegram::messages::AdminMenu& message) {
    try {
        if (!bot_->getApi().sendMessage(message.recipient, translation::menu::kCaption, nullptr, nullptr, admin_start_menu_, "HTML"))
            LOG_ERROR << "/start reply send failed to user " << message.recipient;
    } catch (std::exception& e) {
        LOG_EXCEPTION("Exception while sending admin menu", e);
    }
}

void MessagesSender::operator()(const telegram::messages::Answer& message) {
    try {
        if (!bot_->getApi().answerCallbackQuery(message.callback_id))
            LOG_ERROR << "Answer callback query send failed";
    } catch (std::exception& e) {
        // Timed-out queries trigger this exception, so this exception might be non-fatal, but still logged
        LOG_EXCEPTION("Exception (non-fatal?) while sending answer callback query", e);
    }
}

}  // namespace telegram
