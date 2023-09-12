#pragma once

#include "CodeprojectAiFacade.h"
#include "FrameReader.h"
#include "Settings.h"
#include "TelegramBot.h"
#include "VideoWriter.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>

class Core final {
public:
    explicit Core(Settings settings);
    ~Core();

    Core(const Core&) = delete;
    Core(Core&&) = delete;
    Core& operator=(const Core&) = delete;
    Core& operator=(Core&&) = delete;

    void start();
    void stop();

private:
    void captureThreadFunc();
    void processingThreadFunc();

    void postOnDemandPhoto(const cv::Mat& frame);
    void postAlarmPhoto(const cv::Mat& frame);
    std::filesystem::path saveVideoPreview(const std::string& video_file_uid);
    void postVideoPreview(const std::filesystem::path& file_path);

    static void drawBoxes(const cv::Mat& frame, const nlohmann::json& predictions);
    void initVideoWriter();
    bool isCooldownFinished() const;
    bool isAlarmImageDelayPassed() const;

    const Settings settings_;
    FrameReader frame_reader_;
    TelegramBot bot_;
    CodeprojectAiFacade ai_facade_;
    std::unique_ptr<VideoWriter> video_writer_;

    std::jthread capture_thread_;
    std::jthread processing_thread_;
    std::atomic_bool stop_ = true;

    std::optional<std::chrono::time_point<std::chrono::steady_clock>> first_cooldown_frame_timestamp_;
    std::chrono::time_point<std::chrono::steady_clock> last_alarm_photo_sent_ = std::chrono::steady_clock::now() - std::chrono::hours(100);  // std::chrono::time_point<std::chrono::steady_clock>::max();
    std::string last_alarm_video_uid_;

    std::deque<cv::Mat> buffer_;
    std::mutex buffer_mutex_;
    std::condition_variable buffer_cv_;

    size_t get_frame_error_count_ = 0;
};
