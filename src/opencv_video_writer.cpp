#include "opencv_video_writer.h"

#include "log.h"
#include "uid_utils.h"

#include <stdexcept>

OpenCvVideoWriter::OpenCvVideoWriter(const Settings& settings, const StreamProperties& in_properties, const StreamProperties& out_properties)
    : VideoWriter(settings)
    , use_scale_(in_properties != out_properties)
    , scale_height_(out_properties.height / static_cast<float>(in_properties.height))
    , scale_width_(out_properties.width / static_cast<float>(in_properties.width))
    , scale_algorithm_(scale_width_ < 1.0 ? cv::INTER_AREA : cv::INTER_LANCZOS4) {
    const auto file_name = GenerateFileName(kVideoFilePrefix, &uid_) + kVideoFileExtension;
    if (kVideoCodec.size() != 4) {
        const auto msg = "Invalid codec specified: " + kVideoCodec;
        LOG_ERROR << msg;
        throw std::runtime_error(msg);
    }

    const auto four_cc = cv::VideoWriter::fourcc(kVideoCodec[0], kVideoCodec[1], kVideoCodec[2], kVideoCodec[3]);
    if (!writer_.open((settings.storage_path / file_name).generic_string(), four_cc, out_properties.fps, cv::Size(out_properties.width, out_properties.height))) {
        const auto msg = "Unable to open file for writing: " + file_name;
        LOG_ERROR << msg;
        throw std::runtime_error(msg);
    }
    LogInfo() << "Video writer opened file with uid = " << uid_;
    
}

void OpenCvVideoWriter::AddFrame(const cv::Mat& frame) {
    if (use_scale_) {
        cv::Mat resized_frame;
        cv::resize(frame, resized_frame, cv::Size(0, 0), scale_width_, scale_height_, scale_algorithm_);
        writer_.write(resized_frame);
    } else {
        writer_.write(frame);
    }
    VideoWriter::AddFrame(frame);
}
