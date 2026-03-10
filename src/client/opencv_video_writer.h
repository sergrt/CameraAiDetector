#pragma once

#include "settings.h"
#include "stream_properties.h"
#include "video_writer.h"

#include <opencv2/opencv.hpp>

class OpenCvVideoWriter : public VideoWriter {
public:
    OpenCvVideoWriter(const Settings& settings, const StreamProperties& in_properties, const StreamProperties& out_properties);

    void AddFrame(cv::Mat frame) override;

private:
    cv::VideoWriter writer_;
    const bool use_scale_{false};
    const double scale_height_{1.0};
    const double scale_width_{1.0};
    const int scale_algorithm_{cv::INTER_AREA};
};
