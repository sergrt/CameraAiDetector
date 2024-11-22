#include "frame_reader.h"

#include "log.h"

FrameReader::FrameReader(std::string source)
    : source_(std::move(source))
    , capture_(std::make_unique<cv::VideoCapture>())  {
}

bool FrameReader::Open() {
    capture_->release();
    const auto res = capture_->open(source_, cv::CAP_ANY);
    LOG_INFO_EX << "FrameReader::Open(): " << LOG_VAR(res) << " for source \"" + source_ + "\"";
    if (!res)
        LOG_ERROR_EX << "FrameReader::Open() error: " << LOG_VAR(res) << " for source \"" + source_ + "\"";
    return res;
}

bool FrameReader::Reconnect() {
    capture_->release();
    const auto res = capture_->open(source_, cv::CAP_ANY);
    LOG_INFO_EX << "FrameReader::Reconnect(): " << LOG_VAR(res) << " for source \"" + source_ + "\"";
    if (!res)
        LOG_ERROR_EX << "FrameReader::Reconnect() error: " << LOG_VAR(res) << " for source \"" + source_ + "\"";
    return res;
}

bool FrameReader::GetFrame(cv::Mat& frame) {
    // TODO: check capture_->isOpened() ? Consider performance - this function is called from tight loop
    const auto res = capture_->read(frame);
    LOG_TRACE_EX << "FrameReader::GetFrame(): " << LOG_VAR(res);
    if (!res)
        LOG_ERROR_EX << "FrameReader::GetFrame() error: " << LOG_VAR(res);
    return res;
}

StreamProperties FrameReader::GetStreamProperties() const {
    if (stream_properties_)
        return *stream_properties_;

    LOG_INFO << "Fill stream properties";

    stream_properties_ = StreamProperties();

    stream_properties_->fps = capture_->get(cv::CAP_PROP_FPS);
    LOG_INFO << "Obtained stream FPS: " << stream_properties_->fps;

    stream_properties_->width = static_cast<int>(capture_->get(cv::CAP_PROP_FRAME_WIDTH));
    LOG_INFO << "Obtained stream frame width: " << stream_properties_->width;

    stream_properties_->height = static_cast<int>(capture_->get(cv::CAP_PROP_FRAME_HEIGHT));
    LOG_INFO << "Obtained stream frame height: " << stream_properties_->height;

    return *stream_properties_;
}
