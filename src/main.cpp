#include "Core.h"
#include "FinalAction.h"
#include "Log.h"
#include "Settings.h"

#include <memory>

LogLevel kAppLogLevel = LogLevel::kInfo;
std::ostream* kAppLogStream = nullptr;

int main() {
    std::cout << "Application start, enter \"q\" to quit" << std::endl;

    const Settings settings = LoadSettings();

    kAppLogLevel = static_cast<LogLevel>(settings.log_level);
    if (settings.log_filename.empty()) {
        kAppLogStream = &std::cout;
    } else {
        kAppLogStream = new std::ofstream(settings.log_filename);
    }

    auto _ = FinalAction([] {
        if (kAppLogStream != &std::cout) {
            LogTrace() << "Delete file stream";
            delete kAppLogStream;
        }
    });

    Core core(settings);
    core.Start();

    std::string command;
    while (true) {
        std::cin >> command;
        if (command == "q") {
            std::cout << "Exiting..." << std::endl;
            core.Stop();
            break;
        } else {
            std::cout << "Invalid command. Enter \"q\" to quit" << std::endl;
        }
    }
}
