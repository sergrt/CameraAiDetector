#pragma once

#include "Ai.h"
#include "ErrorState.h"
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

    void Start();
    void Stop();

private:
    void CaptureThreadFunc();
    void ProcessingThreadFunc();

    void PostOnDemandPhoto(const cv::Mat& frame);
    void PostAlarmPhoto(const cv::Mat& frame);
    std::filesystem::path SaveVideoPreview(const std::string& video_file_uid);
    void PostVideoPreview(const std::filesystem::path& file_path);
    void PostVideo(const std::string& uid);

    static void DrawBoxes(const cv::Mat& frame, const std::vector<Detection>& detections);
    void InitVideoWriter();
    bool IsCooldownFinished() const;
    bool IsAlarmImageDelayPassed() const;

    const Settings settings_;
    FrameReader frame_reader_;
    TelegramBot bot_;
    std::unique_ptr<Ai> ai_;
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

    // Use error states in appropriate threads only, no sync is performed for the sake of performance
    ErrorState ai_error_;
    ErrorState frame_reader_error_;
};
