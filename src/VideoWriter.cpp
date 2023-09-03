#include "VideoWriter.h"

#include "Logger.h"

//#include <algorithm>
#include <stdexcept>

const auto fourcc = cv::VideoWriter::fourcc('a', 'v', 'c', '1');

namespace {

cv::Mat createEmptyPreview() {
    cv::Mat empty;
    empty = cv::Mat::zeros(cv::Size(350, 80), CV_8UC1);
    cv::putText(empty, "No preview available", cv::Point(20, 50), 0, 0.8, cv::Scalar(255.0, 255.0, 255.0), 1);
    return empty;
}

}  // namespace

VideoWriter::VideoWriter(const std::filesystem::path& storage_path, const std::string& file_name, const StreamProperties& stream_properties)
    : file_name_((storage_path / file_name).generic_string()) {
    const bool res = writer_.open(file_name_, fourcc, stream_properties.fps, cv::Size(stream_properties.width, stream_properties.height));

    if (!res) {
        const auto msg = "Unable to open file for writing: " + file_name_;
        Logger(LL_ERROR) << msg;
        throw std::runtime_error(msg);
    }
    
    last_frame_time_ = std::chrono::steady_clock::now();
    preview_frames_.reserve(120); // 2 minutes approx
}

std::string VideoWriter::getExtension() {
    return ".mp4";
}

void VideoWriter::write(const cv::Mat& frame) {
    writer_.write(frame);
    
    if (const auto cur_time = std::chrono::steady_clock::now(); cur_time - last_frame_time_ >= std::chrono::milliseconds(2000)) { // TODO: config
        last_frame_time_ = cur_time;
        preview_frames_.push_back(frame);
        // TODO: check size
    }
}

cv::Mat VideoWriter::getPreviewImage() const {
    if (preview_frames_.empty()) {
        Logger(LL_WARNING) << "Preview frames buffer is empty";
        return createEmptyPreview();
    }

    constexpr size_t preview_images = 9;  // 3x3 grid
    
    const double step = preview_frames_.size() / static_cast<double>(preview_images);
    Logger(LL_INFO) << "Preview frames count = " << preview_frames_.size() << ", step = " << step;

    std::vector<cv::Mat> rows;
    for (int i = 0; i < preview_images; ++i) {
        const size_t idx = static_cast<size_t>(step * i);
        if (i % 3 == 0) {
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
        //cv::vconcat(result, rows[i], result);
    }

    const double scale = 1920 / float(result.cols);  // TODO: Make preview size configurable
    cv::Mat resized_res;
    cv::resize(result, resized_res, cv::Size(0, 0), scale, scale);
    return resized_res;
}

std::string VideoWriter::fileNameStripped() const {
    const auto file_name = std::filesystem::path(file_name_).filename().generic_string();
    return file_name.substr(0, file_name.size() - VideoWriter::getExtension().size());
}
