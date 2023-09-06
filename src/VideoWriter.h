#pragma once

#include "StreamProperties.h"

#include <opencv2/opencv.hpp>

#include <chrono>
#include <filesystem>
#include <string>

class VideoWriter final {
public:
    VideoWriter(const std::filesystem::path& storage_path, const StreamProperties& stream_properties);

    void write(const cv::Mat& frame);

    std::string getUid() const;
    cv::Mat getPreviewImage() const;

    static std::string getExtension();
    static std::string generatePreviewFileName(const std::string& uid);
    static std::string getUidFromVideoFileName(const std::string& file_name);
    static std::string getVideoFilePrefix();

private:
    cv::VideoWriter writer_;
    std::string uid_;

    std::chrono::time_point<std::chrono::steady_clock> last_frame_time_;
    std::vector<cv::Mat> preview_frames_;
};
