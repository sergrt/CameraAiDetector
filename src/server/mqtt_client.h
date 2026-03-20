#include "mqtt_callback_holder.h"

#include <mqtt/async_client.h>

#include <memory>

class MqttClient {
public:
    MqttClient();

    bool Start();
    bool Stop();

private:
    void PrepareMqttOptions();
    mqtt::connect_options connect_options_{};
    std::unique_ptr<mqtt::async_client> client_;
    CallbackHolder callback_holder_;
};

