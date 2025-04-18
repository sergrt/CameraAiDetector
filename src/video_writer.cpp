#include "video_writer.h"

#include "log.h"
#include "uid_utils.h"

#include <string>

constexpr size_t kInitialBufferSize = 120;  // Some reasonable value to fit frames without reallocate too often
constexpr size_t kPreviewImages = 9;  // 3x3 grid. Should be square number
const std::string VideoWriter::kVideoFilePrefix = "v_";

std::string VideoWriter::kVideoCodec = "avc1";
std::string VideoWriter::kVideoFileExtension = ".mp4";

namespace {

cv::Mat CreateEmptyPreview() {
    cv::Mat empty = cv::Mat::zeros(cv::Size(350, 80), CV_8UC1);
    cv::putText(empty, "No preview available", cv::Point(20, 50), 0, 0.8, cv::Scalar(255.0, 255.0, 255.0), 1);
    return empty;
}

}  // namespace

bool VideoWriter::IsVideoFile(const std::filesystem::path& file) {
    return file.extension() == kVideoFileExtension;
}

std::string VideoWriter::GeneratePreviewFileName(const std::string& uid) {
    return "preview_" + uid + ".jpg";
}

std::string VideoWriter::GenerateVideoFileName(const std::string& uid) {
    return kVideoFilePrefix + uid + kVideoFileExtension;
}

VideoWriter::VideoWriter(const Settings& settings)
    : preview_sampling_interval_(settings.preview_sampling_interval_ms) {
    last_frame_time_ = std::chrono::steady_clock::now();
    preview_frames_.reserve(kInitialBufferSize);
}

std::string VideoWriter::GetUid() const {
    return uid_;
}

void VideoWriter::AddFrame(cv::Mat frame) {
    if (const auto cur_time = std::chrono::steady_clock::now(); cur_time - last_frame_time_ >= preview_sampling_interval_) {
        last_frame_time_ = cur_time;
        preview_frames_.push_back(std::move(frame));
        // TODO: check size to prevent mem issues
    }
}

cv::Mat VideoWriter::GetPreviewImage() const {
    if (preview_frames_.empty()) {
        LOG_WARNING_EX << "Preview frames buffer is empty";
        return CreateEmptyPreview();
    }

    const double step = static_cast<double>(preview_frames_.size()) / kPreviewImages;
    LOG_INFO << "Preview frames count = " << preview_frames_.size() << ", step = " << step;

    std::vector<cv::Mat> rows;
    const auto images_in_row = static_cast<int>(std::sqrt(kPreviewImages));
    for (size_t i = 0 ; i < kPreviewImages; ++i) {
        const auto idx = static_cast<size_t>(step * i);
        if (i % images_in_row == 0) {
            LOG_DEBUG << "Add row, i = " << i;
            rows.push_back(preview_frames_[idx]);
        }

        auto& row = rows.back();
        cv::hconcat(row, preview_frames_[idx], row);
    }

    cv::Mat result = rows[0];
    for (size_t i = 1; i < rows.size(); ++i) {
        if (rows[i].cols == result.cols)
            result.push_back(rows[i]);
        // cv::vconcat(result, rows[i], result);
    }

    const double scale = 1920 / static_cast<float>(result.cols);  // TODO: Make preview size configurable
    cv::Mat resized_res;
    cv::resize(result, resized_res, cv::Size(0, 0), scale, scale, cv::INTER_AREA);
    return resized_res;
}
