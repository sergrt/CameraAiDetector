#pragma once
#include "settings.h"
#include "stream_properties.h"

#include <opencv2/opencv.hpp>

#include <chrono>
#include <string>
#include <vector>

class VideoWriter {
public:
    VideoWriter(const Settings& settings);
    virtual ~VideoWriter() = default;

    virtual void Start() {}
    virtual void Stop() {}
    virtual void AddFrame(cv::Mat frame);

    virtual std::string GetUid() const;
    virtual cv::Mat GetPreviewImage() const;

    static bool IsVideoFile(const std::filesystem::path& file);
    static std::string GeneratePreviewFileName(const std::string& uid);
    static std::string GenerateVideoFileName(const std::string& uid);

    static const std::string kVideoFilePrefix;
    static std::string kVideoFileExtension;
    static std::string kVideoCodec;

protected:
    std::string uid_;

private:
    const std::chrono::milliseconds preview_sampling_interval_{std::chrono::milliseconds(2000)};
    std::chrono::time_point<std::chrono::steady_clock> last_frame_time_{};
    std::vector<cv::Mat> preview_frames_;
};
