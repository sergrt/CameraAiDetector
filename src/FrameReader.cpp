#include "FrameReader.h"

#include "Logger.h"

FrameReader::FrameReader(std::string source)
    : capture_(std::make_unique<cv::VideoCapture>())
    , source_(std::move(source)) {
}

bool FrameReader::open() {
    capture_->release();
    const auto res = capture_->open(source_, cv::CAP_ANY);
    
    if (!res)
        Logger(LL_ERROR) << "FrameReader::open(): failed to open source \"" + source_ + "\"";
    else
        Logger(LL_INFO) << "FrameReader::open(): opened \"" + source_ + "\"";

    return res;
}

bool FrameReader::reconnect() {
    capture_->release();

    const auto res = capture_->open(source_, cv::CAP_ANY);

    if (!res)
        Logger(LL_ERROR) << "FrameReader::reconnect(): failed to reconnect to source \"" + source_ + "\"";
    else
        Logger(LL_INFO) << "FrameReader::reconnect(): success for \"" + source_ + "\"";

    return res;
}

bool FrameReader::getFrame(cv::Mat& frame) {
    // TODO: check capture_->isOpened() ? Consider performance - this function is called from tight loop
    const auto res = capture_->read(frame);

    if (!res)
        Logger(LogLevel::LL_ERROR) << "FrameReader::getFrame(): failed";
    else
        Logger(LogLevel::LL_TRACE) << "FrameReader::getFrame(): success, frame timestamp = " << capture_->get(cv::CAP_PROP_POS_MSEC);
    
    return res;
}

StreamProperties FrameReader::getStreamProperties() const {
    if (stream_properties_)
        return *stream_properties_;

    Logger(LL_INFO) << "Fill stream properties";
    stream_properties_ = StreamProperties();
    stream_properties_->fps = capture_->get(cv::CAP_PROP_FPS);
    Logger(LL_INFO) << "Obtained stream FPS: " << stream_properties_->fps;
    stream_properties_->width = static_cast<int>(capture_->get(cv::CAP_PROP_FRAME_WIDTH));
    Logger(LL_INFO) << "Obtained stream frame width: " << stream_properties_->width;
    stream_properties_->height = static_cast<int>(capture_->get(cv::CAP_PROP_FRAME_HEIGHT));
    Logger(LL_INFO) << "Obtained stream frame height: " << stream_properties_->height;

    return *stream_properties_;
}
