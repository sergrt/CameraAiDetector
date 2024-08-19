#pragma once

#include "settings.h"
#include "stream_properties.h"
#include "video_writer.h"

#include <opencv2/opencv.hpp>

#ifdef _WINDOWS
#include <windows.h>
#endif

#include <string>

class FfmpegVideoWriter : public VideoWriter {
public:
    FfmpegVideoWriter(const Settings& settings, const StreamProperties& out_properties);
    ~FfmpegVideoWriter();

    void Start() override;
    void Stop() override;

private:
    std::string source_{};
    std::string file_name_{};
    std::string ffmpeg_path_{};
    std::string output_resolution_{"1920x1080"};

#ifdef _WINDOWS
    DWORD ffmpeg_pid_{};
#else
    pid_t ffmpeg_pid_{};
#endif
};
