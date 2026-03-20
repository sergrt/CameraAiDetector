#pragma once

#include "mqtt_client.h"

#include <string>

class ErrorReporter final {
public:
    enum class ErrorState {
        kError = 0,
        kNoError = 1,
    };

    ErrorReporter(MqttClient* mqtt_client, std::string activation_msg, std::string deactivation_msg);

    void Update(ErrorState error_state);

private:
    MqttClient* const mqtt_client_;
    std::string activation_msg_;
    std::string deactivation_msg_;
    ErrorState cur_state_{ErrorState::kNoError};
};
