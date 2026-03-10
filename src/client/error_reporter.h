#pragma once

#include "telegram_bot_facade.h"

#include <string>

class ErrorReporter final {
public:
    enum class ErrorState {
        kError = 0,
        kNoError = 1,
    };

    ErrorReporter(telegram::BotFacade* telegram_bot, std::string activation_msg, std::string deactivation_msg);

    void Update(ErrorState error_state);

private:
    telegram::BotFacade* const bot_;
    std::string activation_msg_;
    std::string deactivation_msg_;
    ErrorState cur_state_{ErrorState::kNoError};
};
