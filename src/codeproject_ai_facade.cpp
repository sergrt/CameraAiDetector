#include "codeproject_ai_facade.h"

#include "log.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace {

size_t WriteCallback(char* contents, size_t size, size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(contents, size * nmemb);
    return size * nmemb;
}

std::vector<Detection> ParseResponse(const nlohmann::json& response) {
    if (!response.value("success", false) || !response.contains("predictions")) {
        LOG_ERROR << "CodeProject AI backend error. Response: " << response.dump();
        return {};
    }

    std::vector<Detection> detections;
    for (const auto& prediction : response["predictions"]) {
        detections.emplace_back(
            prediction["label"],
            prediction["confidence"],
            cv::Rect(cv::Point(prediction["x_min"], prediction["y_min"]),
                     cv::Point(prediction["x_max"], prediction["y_max"]))
        );
    }
    return detections;
}

static const auto curl_deleter = [](CURL* curl) {
     if (curl) {
        curl_easy_cleanup(curl);
    }
 };

}  // namespace

CodeprojectAiFacade::CodeprojectAiFacade(std::string url, float min_confidence, const std::string& img_format)
    : url_(std::move(url))
    , min_confidence_(std::to_string(min_confidence))
    , img_format_("." + img_format)
    , img_mime_type_("image/" + img_format)
    , curl_{curl_ptr(nullptr, curl_deleter)} {

    curl_global_init(CURL_GLOBAL_ALL);
    curl_ = curl_ptr(curl_easy_init(), curl_deleter);

    if (!curl_) {
        LOG_ERROR << "curl init failed";
        throw std::runtime_error("curl init failed");
    }
}

CodeprojectAiFacade::~CodeprojectAiFacade() {
    curl_global_cleanup();
}

std::vector<unsigned char> CodeprojectAiFacade::PrepareImage(const cv::Mat& image) const {
    std::vector<unsigned char> img_buffer;

    // TODO: Check performance impact with different image quality
    // const std::vector<int> img_encode_param{cv::IMWRITE_JPEG_QUALITY, 100};
    static const std::vector<int> img_encode_param;

    if (!cv::imencode(img_format_, image, img_buffer, img_encode_param)) {
        LOG_ERROR << "Frame encoding failed";
        return {};
    }

    return img_buffer;
}

bool CodeprojectAiFacade::Detect(const cv::Mat& image, std::vector<Detection>& detections) {
    const std::vector<unsigned char> data = PrepareImage(image);

    auto mime = curl_mime_init(curl_.get());
    const auto _ = FinalAction([&mime] { curl_mime_free(mime); });

    auto image_part = curl_mime_addpart(mime);
    curl_mime_name(image_part, "image");
    curl_mime_filename(image_part, "image");
    curl_mime_data(image_part, reinterpret_cast<const char*>(data.data()), data.size());
    curl_mime_type(image_part, img_mime_type_.c_str());

    auto confidence_part = curl_mime_addpart(mime);
    curl_mime_name(confidence_part, "min_confidence");
    curl_mime_data(confidence_part, min_confidence_.c_str(), min_confidence_.size());
    curl_mime_type(confidence_part, "text/html");

    curl_easy_setopt(curl_.get(), CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl_.get(), CURLOPT_MIMEPOST, mime);

    std::string read_buffer;
    curl_easy_setopt(curl_.get(), CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl_.get(), CURLOPT_WRITEFUNCTION, WriteCallback);

    const auto curl_res = curl_easy_perform(curl_.get());
    if (curl_res == CURLE_OK) {
        LogTrace() << "detect() ok, result: " << read_buffer;
        detections = ParseResponse(nlohmann::json::parse(read_buffer));
    } else {
        LOG_ERROR << "curl_easy_perform() failed: " << curl_easy_strerror(curl_res);
        return false;
    }

    return true;
}
