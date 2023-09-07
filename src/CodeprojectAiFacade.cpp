#include "CodeprojectAiFacade.h"

#include "Logger.h"

#include <stdexcept>

namespace {

size_t WriteCallback(char* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
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
        Logger(LL_ERROR) << "curl init failed";
        throw std::runtime_error("curl init failed");
    }
}

CodeprojectAiFacade::~CodeprojectAiFacade() {
    if (curl_)
        curl_easy_cleanup(curl_);

    curl_global_cleanup();
}

nlohmann::json CodeprojectAiFacade::Detect(const unsigned char* data, size_t data_size) {
    curl_easy_setopt(curl_, CURLOPT_URL, url_.c_str());

    struct curl_httppost* form = nullptr;
    struct curl_httppost* last = nullptr;

    curl_formadd(&form, &last,
        CURLFORM_COPYNAME, "image",
        CURLFORM_BUFFER, "image",
        CURLFORM_BUFFERPTR, data,
        CURLFORM_BUFFERLENGTH, data_size,
        CURLFORM_CONTENTTYPE, img_mime_type_.c_str(),  //"image/jpeg",
        CURLFORM_END);

    curl_formadd(&form, &last,
        CURLFORM_COPYNAME, "min_confidence",
        CURLFORM_COPYCONTENTS, min_confidence_.c_str(),
        CURLFORM_CONTENTTYPE, "text/html",
        CURLFORM_END);

    curl_easy_setopt(curl_, CURLOPT_HTTPPOST, form);

    std::string read_buffer;
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &read_buffer);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);

    CURLcode res = curl_easy_perform(curl_);
    curl_formfree(form);

    if (res == CURLE_OK) {
        Logger(LL_TRACE) << "Detect() ok, result: " << read_buffer;
        return nlohmann::json::parse(read_buffer);
    } else {
        Logger(LL_ERROR) << "curl_easy_perform() failed: " << curl_easy_strerror(res);
    }

    return {};
}
