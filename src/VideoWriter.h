#pragma once

#include "StreamProperties.h"

#include <opencv2/opencv.hpp>

#include <chrono>
#include <filesystem>
#include <string>

class VideoWriter final {
public:
    VideoWriter(const std::filesystem::path& storage_path, const StreamProperties& in_properties, const StreamProperties& out_properties);

    void Write(const cv::Mat& frame);

    std::string GetUid() const;
    cv::Mat GetPreviewImage() const;

    static bool IsVideoFile(const std::filesystem::path& file);
    static std::string GeneratePreviewFileName(const std::string& uid);
    static std::string GenerateVideoFileName(const std::string& uid);

private:
    cv::VideoWriter writer_;
    std::string uid_;
    const bool use_scale_ = false;
    const double scale_height_ = 1.0;
    const double scale_width_ = 1.0;
    const int scale_algorithm_ = cv::INTER_AREA;

    std::chrono::time_point<std::chrono::steady_clock> last_frame_time_;
    std::vector<cv::Mat> preview_frames_;
};
