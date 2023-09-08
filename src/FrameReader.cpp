#include "FrameReader.h"

#include "Log.h"

FrameReader::FrameReader(std::string source)
    : source_(std::move(source))
    , capture_(std::make_unique<cv::VideoCapture>())  {
}

bool FrameReader::open() {
    capture_->release();
    const auto res = capture_->open(source_, cv::CAP_ANY);
    (res ? LogInfo() : LogError()) << "FrameReader::open() result: " << res << " for source \"" + source_ + "\"";
    return res;
}

bool FrameReader::reconnect() {
    capture_->release();
    const auto res = capture_->open(source_, cv::CAP_ANY);
    (res ? LogInfo() : LogError()) << "FrameReader::reconnect() result: " << res << " for source \"" + source_ + "\"";
    return res;
}

bool FrameReader::getFrame(cv::Mat& frame) {
    // TODO: check capture_->isOpened() ? Consider performance - this function is called from tight loop
    const auto res = capture_->read(frame);
    (res ? LogTrace() : LogError()) << "FrameReader::getFrame() result: " << res;
    return res;
}

StreamProperties FrameReader::getStreamProperties() const {
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
