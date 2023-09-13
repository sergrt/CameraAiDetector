#include "VideoWriter.h"

#include "Log.h"
#include "UidUtils.h"

#include <stdexcept>

const auto kFourcc = cv::VideoWriter::fourcc('a', 'v', 'c', '1');
constexpr size_t kInitialBufferSize = 120;  // Some reasonable value to fit frames without reallocate too often
constexpr auto kPreviewSamplingTime = std::chrono::milliseconds(2000);  // TODO: consider move to config
constexpr size_t kPreviewImages = 9;  // 3x3 grid. Should be square number
constexpr auto kVideoFilePrefix = "v_";
constexpr auto kVideoFileExtension = ".mp4";

namespace {

cv::Mat CreateEmptyPreview() {
    cv::Mat empty = cv::Mat::zeros(cv::Size(350, 80), CV_8UC1);
    cv::putText(empty, "No preview available", cv::Point(20, 50), 0, 0.8, cv::Scalar(255.0, 255.0, 255.0), 1);
    return empty;
}

}  // namespace

VideoWriter::VideoWriter(const std::filesystem::path& storage_path, const StreamProperties& stream_properties) {
    const auto file_name = generateFileName(kVideoFilePrefix, &uid_) + kVideoFileExtension;
    if (!writer_.open((storage_path / file_name).generic_string(), kFourcc, stream_properties.fps, cv::Size(stream_properties.width, stream_properties.height))) {
        const auto msg = "Unable to open file for writing: " + file_name;
        LogError() << msg;
        throw std::runtime_error(msg);
    }
    LogInfo() << "Video writer opened file with uid = " << uid_;
    last_frame_time_ = std::chrono::steady_clock::now();
    preview_frames_.reserve(kInitialBufferSize);
}

bool VideoWriter::IsVideoFile(const std::filesystem::path& file) {
    return file.extension() == kVideoFileExtension;
}

std::string VideoWriter::GeneratePreviewFileName(const std::string& uid) {
    return "preview_" + uid + ".jpg";
}

std::string VideoWriter::GenerateVideoFileName(const std::string& uid) {
    return kVideoFilePrefix + uid + kVideoFileExtension;
}

void VideoWriter::Write(const cv::Mat& frame) {
    writer_.write(frame);

    if (const auto cur_time = std::chrono::steady_clock::now(); cur_time - last_frame_time_ >= kPreviewSamplingTime) {
        last_frame_time_ = cur_time;
        preview_frames_.push_back(frame);
        // TODO: check size to prevent mem issues
    }
}

cv::Mat VideoWriter::GetPreviewImage() const {
    if (preview_frames_.empty()) {
        LogWarning() << "Preview frames buffer is empty";
        return CreateEmptyPreview();
    }

    std::vector<int> indexes(kPreviewImages, -1);
    const auto preview_frames_size = preview_frames_.size();
    const size_t images_count = std::min(preview_frames_size, kPreviewImages);
    const double step = static_cast<double>(preview_frames_size) / images_count;
    for (size_t i = 0; i < images_count; ++i) {
        if (images_count == preview_frames_size) {
            indexes[i] = i;
        } else {
            indexes[i] = static_cast<size_t>(step * i);
        }
    }

    LogInfo() << "Preview frames count = " << preview_frames_size << ", indexes = " << indexes;

    std::vector<cv::Mat> rows;
    const auto images_in_row = static_cast<int>(std::sqrt(kPreviewImages));
    const cv::Mat empty_frame = cv::Mat::zeros(preview_frames_[0].size(), CV_8UC1);
    for (size_t i = 0, sz = indexes.size(); i < sz; ++i) {
        const auto idx = indexes[i];
        const auto frame = idx == -1 ? empty_frame : preview_frames_[idx];

        if (i % images_in_row == 0) {
            LogDebug() << "Add row, i = " << i;
            rows.push_back(frame);
        } else {
            auto& row = rows.back();
            cv::hconcat(row, frame, row);
        }
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

std::string VideoWriter::GetUid() const {
    return uid_;
}
