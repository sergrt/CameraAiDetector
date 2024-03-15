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
        LogError() << "CodeProject AI backend error. Response: " << response.dump();
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
        LogError() << "Frame encoding failed";
        return {};
    }

    return img_buffer;
}

bool CodeprojectAiFacade::Detect(const cv::Mat& image, std::vector<Detection>& detections) {  // const unsigned char* data, size_t data_size) {

    const std::vector<unsigned char> data = PrepareImage(image);
    curl_easy_setopt(curl_.get(), CURLOPT_URL, url_.c_str());

    curl_httppost* form = nullptr;
    curl_httppost* last = nullptr;

    curl_formadd(&form, &last,
        CURLFORM_COPYNAME, "image",
        CURLFORM_BUFFER, "image",
        CURLFORM_BUFFERPTR, data.data(),
        CURLFORM_BUFFERLENGTH, data.size(),
        CURLFORM_CONTENTTYPE, img_mime_type_.c_str(),  // "image/jpeg", "image/bmp" etc.
        CURLFORM_END);

    curl_formadd(&form, &last,
        CURLFORM_COPYNAME, "min_confidence",
        CURLFORM_COPYCONTENTS, min_confidence_.c_str(),
        CURLFORM_CONTENTTYPE, "text/html",
        CURLFORM_END);

    curl_easy_setopt(curl_.get(), CURLOPT_HTTPPOST, form);

    std::string read_buffer;
    curl_easy_setopt(curl_.get(), CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl_.get(), CURLOPT_WRITEFUNCTION, WriteCallback);

    const CURLcode res = curl_easy_perform(curl_.get());
    curl_formfree(form);

    if (res == CURLE_OK) {
        LogTrace() << "detect() ok, result: " << read_buffer;
        detections = ParseResponse(nlohmann::json::parse(read_buffer));
    } else {
        LogError() << "curl_easy_perform() failed: " << curl_easy_strerror(res);
        return false;
    }

    return true;
}
