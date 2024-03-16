#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <variant>

namespace tg_messages {

struct MultipleRecipients {
    std::set<uint64_t> recipients;
};

struct Message : public MultipleRecipients {
    std::string text;
};

struct OnDemandPhoto : public MultipleRecipients {
    std::filesystem::path file_path;
};

struct AlarmPhoto {
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

struct Answer {
    std::string callback_id;
};

} // tg_messages

using TelegramMessage = std::variant<
    tg_messages::Message,
    tg_messages::OnDemandPhoto,
    tg_messages::AlarmPhoto,
    tg_messages::Preview,
    tg_messages::Video,
    tg_messages::Menu,
    tg_messages::Answer>;


template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

// explicit deduction guide (not needed as of C++20)
// template <class... Ts>
//overloaded(Ts...) -> overloaded<Ts...>;
