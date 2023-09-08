#include "CodeprojectAiFacade.h"

#include "FinalAction.h"
#include "Log.h"

#include <stdexcept>

namespace {

size_t writeCallback(char* contents, size_t size, size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(contents, size * nmemb);
    return size * nmemb;
}

}  // namespace

CodeprojectAiFacade::CodeprojectAiFacade(std::string url, std::string min_confidence, const std::string& img_format)
    : url_(std::move(url))
    , min_confidence_(std::move(min_confidence))
    , img_mime_type_("image/" + img_format) {

    curl_global_init(CURL_GLOBAL_ALL);
    curl_ = curl_easy_init();

    if (!curl_) {
        LogError() << "curl init failed";
        throw std::runtime_error("curl init failed");
    }
}

CodeprojectAiFacade::~CodeprojectAiFacade() {
    if (curl_)
        curl_easy_cleanup(curl_);

    curl_global_cleanup();
}

/*
Old implementation - in case anything wrong with the new one
nlohmann::json CodeprojectAiFacade::detect(const unsigned char* data, size_t data_size) {
    curl_easy_setopt(curl_, CURLOPT_URL, url_.c_str());

    curl_httppost* form = nullptr;
    curl_httppost* last = nullptr;

    curl_formadd(&form, &last,
        CURLFORM_COPYNAME, "image",
        CURLFORM_BUFFER, "image",
        CURLFORM_BUFFERPTR, data,
        CURLFORM_BUFFERLENGTH, data_size,
        CURLFORM_CONTENTTYPE, img_mime_type_.c_str(),  // "image/jpeg", "image/bmp" etc.
        CURLFORM_END);

    curl_formadd(&form, &last,
        CURLFORM_COPYNAME, "min_confidence",
        CURLFORM_COPYCONTENTS, min_confidence_.c_str(),
        CURLFORM_CONTENTTYPE, "text/html",
        CURLFORM_END);

    curl_easy_setopt(curl_, CURLOPT_HTTPPOST, form);

    std::string read_buffer;
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);

    const CURLcode res = curl_easy_perform(curl_);
    curl_formfree(form);

    if (res == CURLE_OK) {
        LogTrace() << "detect() ok, result: " << read_buffer;
        return nlohmann::json::parse(read_buffer);
    } else {
        LogError() << "curl_easy_perform() failed: " << curl_easy_strerror(res);
    }

    return {};
}
*/

nlohmann::json CodeprojectAiFacade::detect(const unsigned char* data, size_t data_size) {
    curl_mime* mime = curl_mime_init(curl_);
    auto _ = FinalAction([mime] { curl_mime_free(mime); });

    curl_mimepart* image_part = curl_mime_addpart(mime);
    curl_mime_data(image_part, reinterpret_cast<const char*>(data), data_size);
    curl_mime_type(image_part, img_mime_type_.c_str());
    curl_mime_name(image_part, "image");
    
    curl_mimepart* min_confidence_part = curl_mime_addpart(mime);
    curl_mime_data(min_confidence_part, min_confidence_.c_str(), CURL_ZERO_TERMINATED);
    curl_mime_type(min_confidence_part, "text/html");
    curl_mime_name(min_confidence_part, "min_confidence");

    curl_easy_setopt(curl_, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl_, CURLOPT_URL, url_.c_str());

    std::string read_buffer;
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);

    const CURLcode res = curl_easy_perform(curl_);

    if (res == CURLE_OK) {
        LogTrace() << "detect() ok, result: " << read_buffer;
        return nlohmann::json::parse(read_buffer);
    } else {
        LogError() << "curl_easy_perform() failed: " << curl_easy_strerror(res);
    }

    return {};
}
