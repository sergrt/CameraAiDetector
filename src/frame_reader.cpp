#include "frame_reader.h"

#include "log.h"

FrameReader::FrameReader(std::string source)
    : source_(std::move(source))
    , capture_(std::make_unique<cv::VideoCapture>())  {
}

bool FrameReader::Open() {
    capture_->release();
    const auto res = capture_->open(source_, cv::CAP_ANY);
    (res ? LOG_INFO : LOG_ERROR) << "FrameReader::Open(): " << LOG_VAR(res) << " for source \"" + source_ + "\"";
    return res;
}

bool FrameReader::Reconnect() {
    capture_->release();
    const auto res = capture_->open(source_, cv::CAP_ANY);
    (res ? LOG_INFO : LOG_ERROR) << "FrameReader::Reconnect(): " << LOG_VAR(res) << " for source \"" + source_ + "\"";
    return res;
}

bool FrameReader::GetFrame(cv::Mat& frame) {
    // TODO: check capture_->isOpened() ? Consider performance - this function is called from tight loop
    const auto res = capture_->read(frame);
    (res ? LOG_TRACE : LOG_ERROR) << "FrameReader::GetFrame(): " << LOG_VAR(res);
    return res;
}

StreamProperties FrameReader::GetStreamProperties() const {
    if (stream_properties_)
        return *stream_properties_;

    LogInfo() << "Fill stream properties";

    stream_properties_ = StreamProperties();

    stream_properties_->fps = capture_->get(cv::CAP_PROP_FPS);
    LogInfo() << "Obtained stream FPS: " << stream_properties_->fps;

    stream_properties_->width = static_cast<int>(capture_->get(cv::CAP_PROP_FRAME_WIDTH));
    LogInfo() << "Obtained stream frame width: " << stream_properties_->width;

    stream_properties_->height = static_cast<int>(capture_->get(cv::CAP_PROP_FRAME_HEIGHT));
    LogInfo() << "Obtained stream frame height: " << stream_properties_->height;

    return *stream_properties_;
}
