#include "mqtt_callback_holder.h"

#include "mqtt_messages.h"
#include "telegram_messages.h"

#include "common/log.h"

void CallbackHolder::message_arrived(mqtt::const_message_ptr msg) {
    const auto topic = msg->get_topic();
    LOG_DEBUG << "Received msg: Topic = \"" << topic << "\" "
        << "Payload: " << msg->get_payload();


    if (topic == topics::kStatus) {
        //queue_.Add(telegram::messages::TextMessage{std::move(recipients), message});
    }
        // Here you would send to Telegram
        // send_to_telegram(msg->get_payload());
}