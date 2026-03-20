#include "telegram_messages_queue.h"

#include <mqtt/callback.h>

class CallbackHolder : public virtual mqtt::callback {
public:
    void message_arrived(mqtt::const_message_ptr msg) override;
private:
    telegram::TelegramMessagesQueue queue_;
};
