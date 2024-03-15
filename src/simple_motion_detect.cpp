#include "simple_motion_detect.h"

#include "log.h"

#include <limits>
#include <vector>

constexpr bool kUseTrigger = true;

SimpleMotionDetect::SimpleMotionDetect(const Settings::MotionDetectSettings& settings)
    : gaussian_sz_(cv::Size(settings.gaussian_blur_sz, settings.gaussian_blur_sz))
    , threshold_(settings.threshold)
    , area_trigger_(settings.area_trigger)
    , instrument_detect_impl_("Simple motion", 100)
{
}

bool SimpleMotionDetect::Detect(const cv::Mat& image, std::vector<Detection>& detections) {
    detections.clear();
    // auto _ = instrument_detect_impl_.Trigger();
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, gaussian_sz_, 0);

    if (prev_frame_.empty()) {
        prev_frame_ = gray;
        return true;
    }

    cv::Mat frame_delta;
    cv::absdiff(prev_frame_, gray, frame_delta);
    cv::Mat thresh;
    cv::threshold(frame_delta, thresh, threshold_, 255, cv::THRESH_BINARY);
    cv::dilate(thresh, thresh, cv::Mat(), cv::Point(-1, -1), 2);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    if (kUseTrigger) {
        // Skip first frame in case frame is corrupted
        const bool already_triggered = triggered_;
        triggered_ = !contours.empty();
        if (!already_triggered && triggered_) {
            // Don't save frame as prev, because it might be corrupted one
            return true;
        }
    }

    for (size_t i = 0, sz = contours.size(); i < sz; i++) {
        if (cv::contourArea(contours[i]) < area_trigger_)
            continue;

        Detection detection;

        int min_x = std::numeric_limits<int>::max();
        int min_y = std::numeric_limits<int>::max();
        int max_x = std::numeric_limits<int>::min();
        int max_y = std::numeric_limits<int>::min();

        const auto& contour = contours[i];
        for (size_t j = 0; j < contour.size(); ++j) {
            if (contour[j].x < min_x)
                min_x = contour[j].x;
            if (contour[j].y < min_y)
                min_y = contour[j].y;

            if (contour[j].x > max_x)
                max_x = contour[j].x;
            if (contour[j].y > max_y)
                max_y = contour[j].y;
        }

        const auto rect = cv::Rect(cv::Point(min_x, min_y), cv::Point(max_x, max_y));
        const auto area_str = std::to_string(rect.area());
        detections.emplace_back(area_str,
            1.0f,
            rect);
    }
    prev_frame_ = gray;
    return true;
}
