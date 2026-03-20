#include "mqtt_client.h"

#include "common/log.h"
#include "common/ring_buffer.h"
#include "common/safe_ptr.h"

#include <atomic>
#include <csignal>
#include <iostream>

LogLevel kAppLogLevel{LogLevel::kDebug};
std::ostream* kAppLogStream{nullptr};
constexpr size_t kLogTailLines{32};
SafePtr<RingBuffer<std::string>> AppLogTail{kLogTailLines};

std::atomic_bool keep_running{true};

void SignalHandler(int signum) {
    std::cout << "Caught signal " << signum << std::endl;
    keep_running.store(false);
    keep_running.notify_all();
}

int main()
{
    //kAppLogLevel = static_cast<LogLevel>(settings.log_level);
    //if (settings.log_filename.empty()) {
        kAppLogStream = &std::cout;
    //} else {
    //    kAppLogStream = new std::ofstream(settings.log_filename);
    //}

    auto _ = FinalAction([] {
        if (kAppLogStream != &std::cout) {
            LOG_TRACE << "Delete file stream";
            delete kAppLogStream;
        }
    });

    std::ios_base::sync_with_stdio(false);

    std::signal(SIGINT, SignalHandler);
    std::cout << "Application start, press CTRL+C to quit" << std::endl;

    MqttClient client;
    client.Start();

    keep_running.wait(true);
    std::cout << "Exiting..." << std::endl;
}