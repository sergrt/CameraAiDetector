#include "opencv_ai_facade.h"

#include "log.h"

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <execution>
#include <string>
#include <vector>

// YOLOv5 related constants
constexpr float kInputWidth = 640.0;
constexpr float kInputHeight = 640.0;
const cv::Size kInputSize(kInputWidth, kInputHeight);
constexpr int kDetections1DSize = 85;
constexpr int kDetectionsArraySize = 25200;
constexpr float kScoreThreshold = 0.2;
constexpr float kNmsThreshold = 0.4;

static const std::vector<std::string> kClassNames = {
    "person",
    "bicycle",
    "car",
    "motorbike",
    //"aeroplane",
    //"bus",
    //"train",
    //"truck",
    //"boat",
    //"traffic light",
    //"fire hydrant",
    //"stop sign",
    //"parking meter",
    //"bench",
    "bird",
    "cat",
    "dog",
    //"horse",
    //"sheep",
    //"cow",
    //"elephant",
    //"bear",
    //"zebra",
    //"giraffe",
    "backpack",
    "umbrella",
    "handbag",
    "tie",
    "suitcase",
    //"frisbee",
    //"skis",
    //"snowboard",
    "sports ball",
    //"kite",
    //"baseball bat",
    //"baseball glove",
    //"skateboard",
    //"surfboard",
    //"tennis racket",
    "bottle",
    //"wine glass",
    //"cup",
    //"fork",
    //"knife",
    //"spoon",
    //"bowl",
    "banana",
    "apple",
    //"sandwich",
    //"orange",
    //"broccoli",
    //"carrot",
    //"hot dog",
    "pizza",
    //"donut",
    //"cake",
    //"chair",
    //"sofa",
    //"pottedplant",
    //"bed",
    //"diningtable",
    //"toilet",
    //"tvmonitor",
    //"laptop",
    "mouse",
    //"remote",
    //"keyboard",
    //"cell phone",
    //"microwave",
    //"oven",
    //"toaster",
    //"sink",
    //"refrigerator",
    //"book",
    //"clock",
    //"vase",
    //"scissors",
    //"teddy bear",
    //"hair drier",
    //"toothbrush",
};

namespace {

cv::Mat FormatImageYolov5(const cv::Mat& source) {
    const int& col = source.cols;
    const int& row = source.rows;
    int _max = MAX(col, row);
    cv::Mat result = cv::Mat::zeros(_max, _max, CV_8UC3);
    source.copyTo(result(cv::Rect(0, 0, col, row)));
    return result;
}

}  // namespace

OpenCvAiFacade::OpenCvAiFacade(const std::filesystem::path& onnx_path, float min_confidence)
    : min_confidence_(min_confidence)
    , instrument_detect_impl_("DetectImpl", std::chrono::milliseconds(20'000)) {
    net_ = cv::dnn::readNet(onnx_path.generic_string());

    // Try enable CUDA. Fallbacks to CPU if CUDA is not available
    net_.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
    net_.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA_FP16);
}

std::vector<Detection> OpenCvAiFacade::DetectImpl(const cv::Mat &input_image) {
    cv::Mat blob;
    static constexpr double scale = 1.0 / 255.0;
    cv::dnn::blobFromImage(input_image, blob, scale, kInputSize, cv::Scalar(), true, false);
    net_.setInput(blob);
    std::vector<cv::Mat> output_blobs;
    net_.forward(output_blobs, net_.getUnconnectedOutLayersNames());  // This is the most CPU-intensive operation

    const float x_factor = static_cast<float>(input_image.cols) / kInputWidth;
    const float y_factor = static_cast<float>(input_image.rows) / kInputHeight;

    std::vector<int> class_ids;
    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;

    // Parallel version
    /*
    std::vector<int> indexes;
    indexes.reserve(kDetectionsArraySize);
    for (int i = 0; i < kDetectionsArraySize; ++i) {
        indexes.push_back(i * kDetections1DSize);
    }
    std::mutex processing_lock;
    static const int classes_names_sz = static_cast<int>(kClassNames.size());
    float* const output_blobs_data = reinterpret_cast<float*>(output_blobs[0].data);
    std::for_each(std::execution::par, begin(indexes), end(indexes), [&, classes_names_sz = classes_names_sz](const auto& idx) {  // explicit capture solves unused var warning
        float* const data = output_blobs_data + idx;
        const float confidence = data[4];
        if (confidence >= min_confidence_) {
            float* const classes_scores = data + 5;
            const cv::Mat scores(1, classes_names_sz, CV_32FC1, classes_scores);
            cv::Point class_id;
            double max_class_score = 0.0;
            minMaxLoc(scores, nullptr, &max_class_score, nullptr, &class_id);
            if (max_class_score > kScoreThreshold) {
                const float& x = data[0];
                const float& y = data[1];
                const float& w = data[2];
                const float& h = data[3];
                const int left = static_cast<int>((x - 0.5 * w) * x_factor);
                const int top = static_cast<int>((y - 0.5 * h) * y_factor);
                const int width = static_cast<int>(w * x_factor);
                const int height = static_cast<int>(h * y_factor);

                std::lock_guard lock(processing_lock);
                confidences.push_back(confidence);
                class_ids.push_back(class_id.x);
                boxes.emplace_back(left, top, width, height);
            }
        }
    });
    */
    // Sequential version
    float* output_blobs_data = reinterpret_cast<float*>(output_blobs[0].data);
    for (int i = 0; i < kDetectionsArraySize; ++i) {
        const float& confidence = output_blobs_data[4];
        if (confidence >= min_confidence_) {
            float* const classes_scores = output_blobs_data + 5;
            cv::Mat scores(1, kClassNames.size(), CV_32FC1, classes_scores);
            cv::Point class_id;
            double max_class_score = 0.0;
            minMaxLoc(scores, 0, &max_class_score, 0, &class_id);
            if (max_class_score > kScoreThreshold) {
                confidences.push_back(confidence);

                class_ids.push_back(class_id.x);

                const float& x = output_blobs_data[0];
                const float& y = output_blobs_data[1];
                const float& w = output_blobs_data[2];
                const float& h = output_blobs_data[3];
                const int left = static_cast<int>((x - 0.5 * w) * x_factor);
                const int top = static_cast<int>((y - 0.5 * h) * y_factor);
                const int width = static_cast<int>(w * x_factor);
                const int height = static_cast<int>(h * y_factor);
                boxes.emplace_back(left, top, width, height);
            }
        }
        output_blobs_data += kDetections1DSize;
    }

    std::vector<int> nms_results;
    cv::dnn::NMSBoxes(boxes, confidences, kScoreThreshold, kNmsThreshold, nms_results);
    std::mutex result_lock;
    std::vector<Detection> result;
    result.reserve(nms_results.size());
    std::for_each(std::execution::par, begin(nms_results), end(nms_results), [&](const auto& idx) {
        std::lock_guard lock(result_lock);
        result.emplace_back(kClassNames[class_ids[idx]], confidences[idx], boxes[idx]);
    });

    /*
    for (const auto& idx : nms_results) {
        result.emplace_back(kClassNames[class_ids[idx]], confidences[idx], boxes[idx]);
    }
    */

    return result;
}

bool OpenCvAiFacade::Detect(const cv::Mat &image, std::vector<Detection>& detections) {
    instrument_detect_impl_.Begin();
    const auto img = FormatImageYolov5(image);
    detections = DetectImpl(img);
    instrument_detect_impl_.End();

    // Debug detections
    if (kAppLogLevel <= LogLevel::kTrace && !detections.empty()) {
        std::string log_str = "Detections:\n";
        for (const auto& detection : detections) {
            log_str += "\n{ \"" + detection.class_name + "\", " + std::to_string(detection.confidence) + ", [...] }\n";
        }
        LOG_TRACE << log_str;
    }
    //

    return true;
}
