#include "ffmpeg_video_writer.h"

#include "log.h"
#include "uid_utils.h"

#include <signal.h>
#include <unistd.h>

#include <stdexcept>

FfmpegVideoWriter::FfmpegVideoWriter(const Settings& settings, const StreamProperties& out_properties)
    : VideoWriter(settings)
    , output_resolution_{std::to_string(out_properties.width) + "x" + std::to_string(out_properties.height)} {
    file_name_ = settings.storage_path / (GenerateFileName(VideoWriter::kVideoFilePrefix, &uid_) + VideoWriter::kVideoFileExtension);
}

FfmpegVideoWriter::~FfmpegVideoWriter() {
    Stop();
}

void FfmpegVideoWriter::Start() {
    std::cout << "Main program pid = " << getpid() << std::endl;
    pid_t pid2 = fork();
    if (pid2 < 0) {
        LOG_ERROR << "Fork failed";
    } else if (pid2 == 0) {
        LOG_INFO << "Start ffmpeg";
        execl("/usr/bin/ffmpeg", "ffmpeg", 
            "-i", "rtsp://admin:12345678@192.168.1.100:554/ch01/0",
            "-s", output_resolution_.c_str(),
            "-acodec", "aac",
            //"-vcodec", kVideoCodec.c_str(), //"h264",
            "-vcodec", "h264",
            file_name_.c_str(),
            (char*)NULL);
    } else {
        ffmpeg_pid_ = pid2;
        LOG_INFO << "Started ffmpeg," << LOG_VAR(ffmpeg_pid_);
    }
}

void FfmpegVideoWriter::Stop() {
    LOG_INFO << "Killing process " << ffmpeg_pid_;
    kill(ffmpeg_pid_, SIGTERM);
}
