#pragma once

#include "opencv_video_writer.h"
#include "ffmpeg_video_writer.h"
#include "settings.h"
#include "stream_properties.h"
#include "video_writer.h"

#include <memory>
#include <stdexcept>

inline std::unique_ptr<VideoWriter> VideoWriterFactory(/*add type here, */const Settings& settings, const StreamProperties& in_video_properties, const StreamProperties& out_video_properties) {
    if (settings.use_ffmpeg_writer) {
        return std::make_unique<FfmpegVideoWriter>(settings, out_video_properties);
    } else {
        return std::make_unique<OpenCvVideoWriter>(settings, in_video_properties, out_video_properties);
    }
}
