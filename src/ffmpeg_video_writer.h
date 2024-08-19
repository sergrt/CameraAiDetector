#pragma once

#include "settings.h"
#include "stream_properties.h"
#include "video_writer.h"

#include <opencv2/opencv.hpp>

#include <string>

class FfmpegVideoWriter : public VideoWriter {
public:
    FfmpegVideoWriter(const Settings& settings, const StreamProperties& out_properties);
    ~FfmpegVideoWriter();

    void Start() override;
    void Stop() override;

private:
    std::string file_name_{};
    pid_t ffmpeg_pid_{};
    std::string output_resolution_{"1920x1080"};
};
