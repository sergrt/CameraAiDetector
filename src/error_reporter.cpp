#include "error_reporter.h"

ErrorReporter::ErrorReporter(telegram::BotFacade* telegram_bot, std::string activation_msg, std::string deactivation_msg)
    : bot_(telegram_bot)
    , activation_msg_(std::move(activation_msg))
    , deactivation_msg_(std::move(deactivation_msg))
{}

void ErrorReporter::Update(ErrorState error_state) {
   if (cur_state_ != error_state) {
        cur_state_ = error_state;
        bot_->PostTextMessage(error_state == ErrorState::kError ? activation_msg_ : deactivation_msg_);
    }
}
