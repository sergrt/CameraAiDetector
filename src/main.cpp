#include "Core.h"
#include "FinalAction.h"
#include "Log.h"
#include "RingBuffer.h"
#include "SafePtr.h"
#include "Settings.h"

#include <boost/program_options.hpp>

#include <memory>
#include <string>

LogLevel kAppLogLevel = LogLevel::kInfo;
std::ostream* kAppLogStream = nullptr;
constexpr size_t kLogTailLines = 32u;
SafePtr<RingBuffer<std::string>> AppLogTail(kLogTailLines);
constexpr auto kSettingsFileName = "settings.json";

int main(int argc, char* argv[]) {
    namespace po = boost::program_options;

    std::string settings_file_name;
    po::options_description config("Configuration");
    config.add_options()
        ("help,h", "Display available options")
        ("config,c", po::value<std::string>(&settings_file_name)->default_value(kSettingsFileName), "configuration file name")
    ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, config), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << config << std::endl;
        return 0;
    }

    std::ios_base::sync_with_stdio(false);
    std::cout << "Application start, enter \"q\" to quit" << std::endl;

    Settings settings;
    try {
        settings = LoadSettings(settings_file_name);
    } catch (const std::exception& e) {
        std::cout << "Error loading config file: " << e.what();
        return 1;
    }

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
        if (command == "q" || std::cin.fail() || std::cin.eof()) {
            std::cout << "Exiting..." << std::endl;
            core.Stop();
            break;
        } else {
            std::cout << "Invalid command. Enter \"q\" to quit" << std::endl;
        }
    }
}
