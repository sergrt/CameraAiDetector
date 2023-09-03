#pragma once

#include "StreamProperties.h"

#include <opencv2/opencv.hpp>

#include <chrono>
#include <filesystem>
#include <string>

class VideoWriter final {
public:
    VideoWriter(const std::filesystem::path& storage_path, const std::string& file_name, const StreamProperties& stream_properties);
    void write(const cv::Mat& frame);
    std::string fileNameStripped() const;
    static std::string getExtension();
    cv::Mat getPreviewImage() const;

private:
    cv::VideoWriter writer_;
    std::string file_name_;

    std::chrono::time_point<std::chrono::steady_clock> last_frame_time_;
    std::vector<cv::Mat> preview_frames_;
};
