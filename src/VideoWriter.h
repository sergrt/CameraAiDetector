#pragma once

#include "StreamProperties.h"

#include <opencv2/opencv.hpp>

#include <chrono>
#include <filesystem>
#include <string>

class VideoWriter final {
public:
    VideoWriter(const std::filesystem::path& storage_path, const StreamProperties& stream_properties);

    void Write(const cv::Mat& frame);

    std::string GetUid() const;
    cv::Mat GetPreviewImage() const;

    static bool IsVideoFile(const std::filesystem::path& file);
    static std::string GeneratePreviewFileName(const std::string& uid);
    static std::string GenerateVideoFileName(const std::string& uid);

private:
    cv::VideoWriter writer_;
    std::string uid_;

    std::chrono::time_point<std::chrono::steady_clock> last_frame_time_;
    std::vector<cv::Mat> preview_frames_;
};
