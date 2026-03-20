#include "telegram_messages.h"

#include <deque>

namespace telegram {

class TelegramMessagesQueue {
private:
    std::deque<Message> messages_queue_;
};

}  // namespace telegram
