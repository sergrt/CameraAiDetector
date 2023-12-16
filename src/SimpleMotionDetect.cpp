#include "SimpleMotionDetect.h"

#include "Log.h"

#include <limits>
#include <vector>

SimpleMotionDetect::SimpleMotionDetect(const Settings::MotionDetectSettings& settings)
    : gaussian_sz_(cv::Size(settings.gaussian_blur_sz, settings.gaussian_blur_sz))
    , threshold_(settings.threshold)
    , area_trigger_(settings.area_trigger)
    , instrument_detect_impl_("Simple motion", 100)
{
}

bool SimpleMotionDetect::Detect(const cv::Mat& image, std::vector<Detection>& detections) {
    auto _ = instrument_detect_impl_.Trigger();
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

        detections.emplace_back("generic",
            1.0f,
            cv::Rect(cv::Point(min_x, min_y), cv::Point(max_x, max_y)));
    }
    prev_frame_ = gray;
    return true;
}
