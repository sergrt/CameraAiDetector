#include "VideoWriter.h"

#include "Log.h"
#include "UidUtils.h"

#include <stdexcept>

constexpr size_t kInitialBufferSize = 120;  // Some reasonable value to fit frames without reallocate too often
constexpr size_t kPreviewImages = 9;  // 3x3 grid. Should be square number
constexpr auto kVideoFilePrefix = "v_";

std::string VideoWriter::kVideoCodec = "avc1";
std::string VideoWriter::kVideoFileExtension = ".mp4";

namespace {

cv::Mat CreateEmptyPreview() {
    cv::Mat empty = cv::Mat::zeros(cv::Size(350, 80), CV_8UC1);
    cv::putText(empty, "No preview available", cv::Point(20, 50), 0, 0.8, cv::Scalar(255.0, 255.0, 255.0), 1);
    return empty;
}

}  // namespace

VideoWriter::VideoWriter(const Settings& settings, const StreamProperties& in_properties, const StreamProperties& out_properties)
    : use_scale_(in_properties != out_properties)
    , scale_height_(out_properties.height / static_cast<float>(in_properties.height))
    , scale_width_(out_properties.width / static_cast<float>(in_properties.width))
    , scale_algorithm_(scale_width_ < 1.0 ? cv::INTER_AREA : cv::INTER_LANCZOS4)
    , preview_sampling_interval_(settings.preview_sampling_interval_ms) {
    const auto file_name = GenerateFileName(kVideoFilePrefix, &uid_) + kVideoFileExtension;
    if (kVideoCodec.size() != 4) {
        const auto msg = "Invalid codec specified: " + kVideoCodec;
        LogError() << msg;
        throw std::runtime_error(msg);
    }

    const auto four_cc = cv::VideoWriter::fourcc(kVideoCodec[0], kVideoCodec[1], kVideoCodec[2], kVideoCodec[3]);
    if (!writer_.open((settings.storage_path / file_name).generic_string(), four_cc, out_properties.fps, cv::Size(out_properties.width, out_properties.height))) {
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
    if (use_scale_) {
        cv::Mat resized_frame;
        cv::resize(frame, resized_frame, cv::Size(0, 0), scale_width_, scale_height_, scale_algorithm_);
        writer_.write(resized_frame);
    } else {
        writer_.write(frame);
    }

    if (const auto cur_time = std::chrono::steady_clock::now(); cur_time - last_frame_time_ >= preview_sampling_interval_) {
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

    const double step = static_cast<double>(preview_frames_.size()) / kPreviewImages;
    LogInfo() << "Preview frames count = " << preview_frames_.size() << ", step = " << step;

    std::vector<cv::Mat> rows;
    constexpr auto images_in_row = static_cast<int>(std::sqrt(kPreviewImages));
    for (size_t i = 0 ; i < kPreviewImages; ++i) {
        const auto idx = static_cast<size_t>(step * i);
        if (i % images_in_row == 0) {
            LogDebug() << "Add row, i = " << i;
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

std::string VideoWriter::GetUid() const {
    return uid_;
}
