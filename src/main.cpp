#include "Core.h"
#include "FinalAction.h"
#include "Logger.h"
#include "Settings.h"

#include <memory>

LogLevel app_log_level = LL_INFO;
std::ostream* app_log_stream = nullptr;

int main() {
    std::cout << "Application start, enter \"q\" to quit" << std::endl;

    const Settings settings = loadSettings();

    app_log_level = static_cast<LogLevel>(settings.log_severity);
    if (settings.log_filename.empty()) {
        app_log_stream = &std::cout;
    } else {
        app_log_stream = new std::ofstream(settings.log_filename);
    }

    auto _ = FinalAction([]() {
        if (app_log_stream != &std::cout) {
            Logger(LL_INFO) << "Delete file stream";
            delete app_log_stream;
        }
    });

    Core core(settings);
    core.start();

    std::string command;
    while (true) {
        std::cin >> command;
        if (command == "q") {
            std::cout << "Exiting..." << std::endl;
            core.stop();
            break;
        } else {
            std::cout << "Invalid command. Enter \"q\" to quit" << std::endl;
        }
    }
}
