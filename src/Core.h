#pragma once

#include "CodeprojectAiFacade.h"
#include "FrameReader.h"
#include "Settings.h"
#include "TelegramBot.h"
#include "VideoWriter.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <deque>
#include <condition_variable>

class Core final {
public:
    Core(const Settings& settings);
    ~Core();
    void start();
    void stop();

private:
    void threadFunc();
    void threadFuncProcess();
    const Settings settings_;
    FrameReader frame_reader_;
    CodeprojectAiFacade ai_facade_;

//public:
    std::atomic_bool stop_ = true;

private:

    void postOnDemandPhoto(const cv::Mat& frame);
    void postAlarmImage(const cv::Mat& frame);
    void drawBoxes(const cv::Mat& frame, const nlohmann::json& predictions);
    void initVideoWriter();
    void postPreview();

    std::jthread thread_;
    std::jthread thread_processing_;
    std::unique_ptr<VideoWriter> video_writer_;
    std::optional<std::chrono::time_point<std::chrono::steady_clock>> first_cooldown_frame_timestamp_;
    std::chrono::time_point<std::chrono::steady_clock> last_alarm_image_sent_ =
        std::chrono::steady_clock::now() -
        std::chrono::hours(100);  // std::chrono::time_point<std::chrono::steady_clock>::max();
    TelegramBot bot_;
    std::atomic_bool post_photo_ = false;

    std::mutex buffer_mutex_;
    std::deque<cv::Mat> buffer_;
    std::condition_variable cv_;

    size_t get_frame_error_count_ = 0;
};