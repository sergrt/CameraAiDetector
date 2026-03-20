#include "mqtt_client.h"
#include "mqtt_messages.h"

#include "../common/log.h"

#include <chrono>

// TODO: check linkage
const std::string SERVER_ADDRESS = "tcp://localhost:1883";
const std::string CLIENT_ID = "telegram_sender";

const auto kDefaultTimeout = std::chrono::seconds(15);

MqttClient::MqttClient() {
    client_ = std::make_unique<mqtt::async_client>(SERVER_ADDRESS, CLIENT_ID);
    client_->set_callback(callback_holder_);

    PrepareMqttOptions();
}

void MqttClient::PrepareMqttOptions() {
    connect_options_.set_clean_session(false);
    connect_options_.set_keep_alive_interval(60);
    connect_options_.set_automatic_reconnect(true);
    //connect_options_.set_ssl(mqtt::ssl_options{})
}

bool MqttClient::Start() {
    auto connect_token = client_->connect(connect_options_);
    if (!connect_token->wait_for(kDefaultTimeout)) {
        LOG_ERROR << "Can't connect to MQTT server";
        return false;
    }

    for (const auto& topic : {topics::kStatus, topics::kImages, topics::kVideos}) {
        auto subscribe_token = client_->subscribe(topic, 1);
        if (!subscribe_token->wait_for(kDefaultTimeout)) {
            LOG_ERROR << "Can't subscribe on topic \"" << topic << "\"";
            //return false;
        }
    }

    LOG_DEBUG << "Waiting for messages...\n";

    /*
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, []{ return !running; });

    */
   return true;
}

bool MqttClient::Stop() {
    auto disconnect_token = client_->disconnect();
    if (!disconnect_token->wait_for(kDefaultTimeout)) {
        LOG_ERROR << "Timeout on disconnect from MQTT";
        return false;
    }
    return true;
}
