#include "VideoWriter.h"

#include "Logger.h"
#include "UidUtils.h"

#include <stdexcept>

const auto fourcc = cv::VideoWriter::fourcc('a', 'v', 'c', '1');
constexpr size_t initial_buffer_size = 120;  // Some reasonable value to fit frames without reallocate too often
constexpr auto preview_sampling_time = std::chrono::milliseconds(2000);  // TODO: consider move to config
constexpr size_t preview_images = 9;  // 3x3 grid. Should be square number
const auto video_file_prefix = std::string("v_");

namespace {

cv::Mat createEmptyPreview() {
    cv::Mat empty;
    empty = cv::Mat::zeros(cv::Size(350, 80), CV_8UC1);
    cv::putText(empty, "No preview available", cv::Point(20, 50), 0, 0.8, cv::Scalar(255.0, 255.0, 255.0), 1);
    return empty;
}

}  // namespace

VideoWriter::VideoWriter(const std::filesystem::path& storage_path, const StreamProperties& stream_properties) {
    const auto file_name = generateFileName(video_file_prefix, &uid_) + getExtension();
    if (!writer_.open(file_name, fourcc, stream_properties.fps, cv::Size(stream_properties.width, stream_properties.height))) {
        const auto msg = "Unable to open file for writing: " + file_name;
        Logger(LL_ERROR) << msg;
        throw std::runtime_error(msg);
    }
    
    last_frame_time_ = std::chrono::steady_clock::now();
    preview_frames_.reserve(initial_buffer_size);
}

std::string VideoWriter::getExtension() {
    return std::string(".mp4");
}

std::string VideoWriter::generatePreviewFileName(const std::string& uid) {
    return "preview_" + uid + ".jpg";
}

std::string VideoWriter::getUidFromVideoFileName(const std::string& file_name) {
    const auto uid = file_name.substr(video_file_prefix.size(), file_name.size() - video_file_prefix.size() - getExtension().size());
    return uid;
}

void VideoWriter::write(const cv::Mat& frame) {
    writer_.write(frame);
    
    if (const auto cur_time = std::chrono::steady_clock::now(); cur_time - last_frame_time_ >= preview_sampling_time) {
        last_frame_time_ = cur_time;
        preview_frames_.push_back(frame);
        // TODO: check size to prevent mem issues
    }
}

cv::Mat VideoWriter::getPreviewImage() const {
    if (preview_frames_.empty()) {
        Logger(LL_WARNING) << "Preview frames buffer is empty";
        return createEmptyPreview();
    }

    const double step = static_cast<double>(preview_frames_.size()) / preview_images;
    Logger(LL_INFO) << "Preview frames count = " << preview_frames_.size() << ", step = " << step;

    std::vector<cv::Mat> rows;
    const auto images_in_row = static_cast<int>(std::sqrt(preview_images));
    for (int i = 0; i < preview_images; ++i) {
        const auto idx = static_cast<size_t>(step * i);
        if (i % images_in_row == 0) {
            Logger(LL_INFO) << "Add row, idx = " << idx;
            rows.push_back(preview_frames_[idx]);
        } else {
            auto& row = rows.back();
            cv::hconcat(row, preview_frames_[idx], row);
        }
    }
    
    cv::Mat result = rows[0];
    for (size_t i = 1; i < rows.size(); ++i) {
        if (rows[i].cols == result.cols)
            result.push_back(rows[i]);
        // cv::vconcat(result, rows[i], result);
    }

    const double scale = 1920 / float(result.cols);  // TODO: Make preview size configurable
    cv::Mat resized_res;
    cv::resize(result, resized_res, cv::Size(0, 0), scale, scale, cv::INTER_AREA);
    return resized_res;
}

std::string VideoWriter::getUid() const {
    return uid_;
}
