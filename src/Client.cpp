#include "Client.h"

#include "Logger.h"

#include <format>
#include <chrono>

const cv::Scalar frame_color = cv::Scalar(0.0, 0.0, 200.0);
const int frame_width = 2;

namespace {

std::string generateFileName(const std::string& prefix) {
    const auto tp = std::chrono::system_clock::now();
    std::string timestamp = std::format("{:%Y%m%dT%H%M%S}", tp);
    for (auto& c : timestamp) {
        if (c == '.')
            c = '_';
    }
    return prefix + timestamp;
}



}  // namespace

Client::Client(const Settings& settings)
    : settings_(settings),
      frame_reader_(settings_.source),
      bot_(settings_.bot_token, settings_.storage_path, settings_.allowed_users),
      ai_facade_(settings_.codeproject_ai_url, settings_.min_confidence, settings_.img_format) {
    bot_.start();
    frame_reader_.open();
}

Client::~Client() {
    bot_.stop();
    stop();
}

void Client::postOnDemandPhoto(const cv::Mat& frame) {
    const auto file_name = generateFileName("on_demand_") + ".jpg";
    cv::imwrite((settings_.storage_path / file_name).generic_string(), frame);
    bot_.postOnDemandPhoto(file_name);
}

void Client::initVideoWriter() {
    Logger(LL_INFO) << "Init video writer";
    const auto stream_properties = frame_reader_.getStreamProperties();
    const auto file_name = generateFileName("v_") + VideoWriter::getExtension();
    video_writer_ = std::make_unique<VideoWriter>(settings_.storage_path, file_name, stream_properties);
}

void Client::postAlarmImage(const cv::Mat& frame) {
    last_alarm_image_sent_ = std::chrono::steady_clock::now();
    const auto file_name = generateFileName("alarm_") + ".jpg";
    cv::imwrite((settings_.storage_path / file_name).generic_string(), frame);
    bot_.postAlarmPhoto(file_name);
} 

void Client::drawBoxes(const cv::Mat& frame, const nlohmann::json& predictions) {
    // Draw boxes
    for (const auto& prediction : predictions) {
        cv::rectangle(frame, cv::Point(prediction["x_min"], prediction["y_min"]), cv::Point(prediction["x_max"], prediction["y_max"]), frame_color, frame_width);
    }
}

void Client::postPreview() {
    const auto file_name = generateFileName("preview_") + ".jpg";
    cv::imwrite((settings_.storage_path / file_name).generic_string(), video_writer_->getPreviewImage());
    bot_.postVideoPreview(file_name, "Video: " + TelegramBot::VideoCmdPrefix() + video_writer_->fileNameStripped());
}

void Client::threadFuncProcess() {
    const auto scaled_size = cv::Size(
        static_cast<int>(frame_reader_.getStreamProperties().width * settings_.img_scale_x),
        static_cast<int>(frame_reader_.getStreamProperties().height * settings_.img_scale_y));
    const auto img_format = "." + settings_.img_format;

    //const std::vector<int> img_encode_param{cv::IMWRITE_JPEG_QUALITY, 100};
    const std::vector<int> img_encode_param;

    while (!stop_) {
        std::unique_lock lock(buffer_mutex_);
        cv_.wait(
            lock, [&] { return !buffer_.empty() || stop_; });

        if (stop_)
            break;

        const cv::Mat frame = std::move(buffer_.front());
        buffer_.pop_front();
        lock.unlock();

        if (bot_.waitingForPhoto()) {
            postOnDemandPhoto(frame);
        }

        static uint64_t i = 0;
        const bool call_detect = (i++ % settings_.nth_detect_frame == 0);

        if (!call_detect) {
            if (video_writer_) {
                Logger(LL_TRACE) << "Detect not called, just write";
                video_writer_->write(frame);
            }
        }

        if (call_detect) {
            std::vector<unsigned char> img_buffer;
            //const bool imencode_res = cv::imencode(".jpg", frame, img_buffer);
            
            cv::Mat frame_scaled;  // used later to draw box on it and send to users
            if (settings_.use_image_scale) {
                cv::resize(frame, frame_scaled, scaled_size);
                const bool imencode_res = cv::imencode(img_format, frame_scaled, img_buffer, img_encode_param);
            } else {
                const bool imencode_res = cv::imencode(img_format, frame, img_buffer, img_encode_param);
            }

            const auto detect_json = ai_facade_.Detect(img_buffer.data(), img_buffer.size());
            Logger(LL_TRACE) << "Detect result: " << detect_json;

            if (!detect_json.empty() && detect_json["success"] == true && detect_json.contains("predictions") && !detect_json["predictions"].empty()) {

                if (first_cooldown_frame_timestamp_) {  // We are writing cooldown sequence, and detected something - stop cooldown
                    Logger(LL_INFO) << "Cooldown stopped - object detected";
                    first_cooldown_frame_timestamp_.reset();
                }

                if (!video_writer_)
                    initVideoWriter();

                video_writer_->write(frame);

                // Check tg delay
                if (std::chrono::steady_clock::now() - last_alarm_image_sent_ > std::chrono::milliseconds(settings_.telegram_notification_delay_ms)) {
                    const cv::Mat& mat_box = (settings_.use_image_scale ? frame_scaled : frame);
                    drawBoxes(mat_box, detect_json["predictions"]);
                    postAlarmImage(mat_box);
                }

            } else {  // Not detected
                if (video_writer_) {
                    if (!first_cooldown_frame_timestamp_) {
                        Logger(LL_INFO) << "Start cooldown writing";
                        first_cooldown_frame_timestamp_ = std::chrono::steady_clock::now();
                        video_writer_->write(frame);
                    } else {
                        Logger(LL_INFO) << "Write cooldown frame";
                        video_writer_->write(frame);
                        if (std::chrono::steady_clock::now() - *first_cooldown_frame_timestamp_ > std::chrono::milliseconds(settings_.cooldown_write_time_ms)) {
                            Logger(LL_INFO) << "Finish writing file \"" << video_writer_->fileNameStripped() << "\"";
                            postPreview();
                            // Stop cooldown
                            video_writer_.reset();
                            first_cooldown_frame_timestamp_.reset();
                        }
                    }
                }
            }
        }
    }
}

void Client::threadFunc() {
    while (!stop_) {
        cv::Mat frame;
        if (!frame_reader_.getFrame(frame)) {
            ++get_frame_error_count_;
            Logger(LogLevel::LL_ERROR) << "Can't get frame";

            if (get_frame_error_count_ >= settings_.errors_before_reconnect) {
                Logger(LL_INFO) << "Reconnect";
                get_frame_error_count_ = 0;
                frame_reader_.reconnect();
            } else {
                Logger(LL_INFO) << "Error delay, count = " << get_frame_error_count_;
                std::this_thread::sleep_for(std::chrono::milliseconds(settings_.delay_after_error_ms));
            }
        } else {
            get_frame_error_count_ = 0;
            size_t buffer_size = 0;
            {
                std::lock_guard lock(buffer_mutex_);
                buffer_.emplace_back(std::move(frame));
                buffer_size = buffer_.size();
            }
            cv_.notify_all();

            static uint64_t counter = 0;
            if (counter++ % 300 == 0)
                Logger(LL_TRACE) << "buffer size = " << buffer_size;

            if (buffer_size % 10 == 0) {
                Logger(LL_INFO) << "buffer size = " << buffer_size;
                if (buffer_size > 100) {
                    //Logger(LL_WARNING) << "buffer size > 100";
                }
                if (buffer_size > 500) {
                    if (!video_writer_) { // Do not drop cache while recording video - it might be processed later
                        Logger(LL_ERROR) << "buffer size > 500, dropping cache";
                        std::lock_guard lock(buffer_mutex_);
                        const size_t half = buffer_.size() / 2;
                        buffer_.erase(buffer_.begin(), buffer_.end() - half);
                    }
                }
            }
            
        }
    }
    stop_ = true;
    stop_.notify_all();
}

void Client::start() {
    if (!stop_) {
        Logger(LL_WARNING) << "Attempt start() on already running client";
        return;
    }

    stop_ = false;
    stop_.notify_all();
    thread_ = std::jthread(&Client::threadFunc, this);
    thread_processing_ = std::jthread(&Client::threadFuncProcess, this);
}

void Client::stop() {
    if (stop_) {
        Logger(LL_WARNING) << "Attempt stop() on already stopped client";
    }
    stop_ = true;
    cv_.notify_all();
    //stop_.notify_all();
    /*
    if (thread_.joinable())
        thread_.join();
    if (thread_processing_.joinable())
        thread_processing_.join();
    */
}
