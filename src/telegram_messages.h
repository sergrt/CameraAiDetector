#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <variant>

namespace telegram {

namespace commands {

inline const auto kStart = std::string("start");
inline const auto kPreviews = std::string("previews");
inline const auto kVideos = std::string("videos");
inline const auto kVideo = std::string("video");
inline const auto kImage = std::string("image");
inline const auto kPing = std::string("ping");
inline const auto kLog = std::string("log");
inline const auto kPause = std::string("pause");
inline const auto kResume = std::string("resume");

inline std::string VideoCmdPrefix() {
    auto video_prefix = "/" + kVideo + "_";
    return video_prefix;
}

}  // namespace commands

namespace messages {

struct MultipleRecipients {
    std::set<uint64_t> recipients;
};

struct TextMessage : public MultipleRecipients {
    std::string text;
};

struct OnDemandPhoto : public MultipleRecipients {
    std::filesystem::path file_path;
};

struct AlarmPhoto : public MultipleRecipients {
    std::filesystem::path file_path;
    std::string detections;
};

struct Preview : public MultipleRecipients {
    std::filesystem::path file_path;
};

struct Video : public MultipleRecipients {
    std::filesystem::path file_path;
};

struct Menu {
    uint64_t recipient{};
};

struct AdminMenu {
    uint64_t recipient{};
};

struct Answer {
    std::string callback_id;
};

}  // namespace messages

using Message = std::variant<
    telegram::messages::TextMessage,
    telegram::messages::OnDemandPhoto,
    telegram::messages::AlarmPhoto,
    telegram::messages::Preview,
    telegram::messages::Video,
    telegram::messages::Menu,
    telegram::messages::AdminMenu,
    telegram::messages::Answer>;

}  // namespace telegram

// template <class... Ts>
// struct Overloaded : Ts... {
//     using Ts::operator()...;
// };

// explicit deduction guide (not needed as of C++20)
// template <class... Ts>
// Overloaded(Ts...) -> Overloaded<Ts...>;
