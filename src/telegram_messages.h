#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <variant>

namespace telegram_messages {

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

} // telegram_messages

using TelegramMessage = std::variant<
    telegram_messages::Message,
    telegram_messages::OnDemandPhoto,
    telegram_messages::AlarmPhoto,
    telegram_messages::Preview,
    telegram_messages::Video,
    telegram_messages::Menu,
    telegram_messages::Answer>;


template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

// explicit deduction guide (not needed as of C++20)
// template <class... Ts>
// Overloaded(Ts...) -> Overloaded<Ts...>;
