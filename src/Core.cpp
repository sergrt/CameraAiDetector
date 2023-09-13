#include "Core.h"

#include "Log.h"
#include "UidUtils.h"

const cv::Scalar kFrameColor = cv::Scalar(0.0, 0.0, 200.0);
constexpr int kFrameWidth = 2;
constexpr size_t kMaxBufferSize = 500u;  // Approx 20 sec of 25 fps stream
constexpr auto kBufferOverflowDelay = std::chrono::seconds(1);

Core::Core(Settings settings)
    : settings_(std::move(settings)),
      frame_reader_(settings_.source),
      bot_(settings_.bot_token, settings_.storage_path, settings_.allowed_users),
      ai_facade_(settings_.codeproject_ai_url, settings_.min_confidence, settings_.img_format) {
    bot_.start();
    frame_reader_.open();
}

Core::~Core() {
    bot_.stop();
    Stop();
}

void Core::PostOnDemandPhoto(const cv::Mat& frame) {
    const auto file_name = generateFileName("on_demand_") + ".jpg";
    const auto path = (settings_.storage_path / file_name).generic_string();
    if (!cv::imwrite(path, frame))
        LogError() << "Error write on-demand photo, path = " << path;
    bot_.postOnDemandPhoto(path);
}

void Core::InitVideoWriter() {
    LogInfo() << "Init video writer";
    const auto stream_properties = frame_reader_.getStreamProperties();
    video_writer_ = std::make_unique<VideoWriter>(settings_.storage_path, stream_properties);
}

void Core::PostAlarmPhoto(const cv::Mat& frame) {
    last_alarm_photo_sent_ = std::chrono::steady_clock::now();
    const auto file_name = generateFileName("alarm_") + ".jpg";
    const auto path = settings_.storage_path / file_name;
    if (!cv::imwrite(path.generic_string(), frame))
        LogError() << "Error write alarm photo, path = " << path;
    bot_.postAlarmPhoto(path);
} 

void Core::DrawBoxes(const cv::Mat& frame, const nlohmann::json& predictions) {
    for (const auto& prediction : predictions) {
        cv::rectangle(frame, cv::Point(prediction["x_min"], prediction["y_min"]), cv::Point(prediction["x_max"], prediction["y_max"]), kFrameColor, kFrameWidth);
    }
}

std::filesystem::path Core::SaveVideoPreview(const std::string& video_file_uid) {
    const auto file_name = VideoWriter::GeneratePreviewFileName(video_file_uid);
    const std::vector<int> img_encode_param{cv::IMWRITE_JPEG_QUALITY, 90};
    const auto path = settings_.storage_path / file_name;
    if (!cv::imwrite(path.generic_string(), video_writer_->GetPreviewImage(), img_encode_param))
        LogError() << "Error write video preview image, path = " << path;
    return path;
}

void Core::PostVideoPreview(const std::filesystem::path& file_path) {
    bot_.postVideoPreview({}, file_path);
}

void Core::ProcessingThreadFunc() {
    const auto scaled_size = cv::Size(
        static_cast<int>(frame_reader_.getStreamProperties().width * settings_.img_scale_x),
        static_cast<int>(frame_reader_.getStreamProperties().height * settings_.img_scale_y));
    const auto img_format = "." + settings_.img_format;

    // TODO: Check performance impact with different image quality
    // const std::vector<int> img_encode_param{cv::IMWRITE_JPEG_QUALITY, 100};
    const std::vector<int> img_encode_param;

    while (!stop_) {
        std::unique_lock lock(buffer_mutex_);
        buffer_cv_.wait(lock, [&] { return !buffer_.empty() || stop_; });

        if (stop_)
            break;

        const cv::Mat frame = std::move(buffer_.front());
        buffer_.pop_front();
        lock.unlock();

        if (bot_.someoneIsWaitingForPhoto())
            PostOnDemandPhoto(frame);

        static uint64_t i = 0;
        const bool check_frame = (i++ % settings_.nth_detect_frame == 0);

        if (!check_frame) {
            if (video_writer_) {
                LogTrace() << "Detect not called, just write";
                video_writer_->Write(frame);
            }
        }

        if (check_frame) {
            std::vector<unsigned char> img_buffer;

            cv::Mat scaled_frame;
            if (settings_.use_image_scale)
                cv::resize(frame, scaled_frame, scaled_size);

            if (!cv::imencode(img_format, (settings_.use_image_scale ? scaled_frame : frame), img_buffer, img_encode_param)) {
                LogError() << "Frame encoding failed";
                continue;
            }
            const auto detect_result = ai_facade_.Detect(img_buffer.data(), img_buffer.size());
            LogTrace() << "Detect result: " << detect_result;

            if (!detect_result.empty() && detect_result["success"] == true && detect_result.contains("predictions") && !detect_result["predictions"].empty()) {
                if (first_cooldown_frame_timestamp_) {  // We are writing cooldown sequence, and detected something - stop cooldown
                    LogInfo() << "Cooldown stopped - object detected";
                    first_cooldown_frame_timestamp_.reset();
                }

                if (!video_writer_)
                    InitVideoWriter();

                video_writer_->Write(frame);
                const auto video_uid = video_writer_->GetUid();
                if (video_uid != last_alarm_video_uid_ || IsAlarmImageDelayPassed()) {
                    auto& alarm_frame = (settings_.use_image_scale ? scaled_frame : frame);
                    DrawBoxes(alarm_frame, detect_result["predictions"]);
                    PostAlarmPhoto(alarm_frame);
                    last_alarm_video_uid_ = video_uid;
                }
            } else {  // Not detected
                if (video_writer_) {
                    video_writer_->Write(frame);

                    if (!first_cooldown_frame_timestamp_) {
                        LogInfo() << "Start cooldown writing";
                        first_cooldown_frame_timestamp_ = std::chrono::steady_clock::now();    
                    } else {
                        LogTrace() << "Cooldown frame saved";
                        if (IsCooldownFinished()) {
                            const auto uid = video_writer_->GetUid();
                            LogInfo() << "Finish writing file with uid = " << uid;
                            if (const auto preview_file_path = SaveVideoPreview(uid); settings_.send_video_previews)
                                PostVideoPreview(preview_file_path);
                            // Stop cooldown
                            video_writer_.reset();
                            first_cooldown_frame_timestamp_.reset();
                        }
                    }
                }
            }  // Not detected
        }
    }
}

bool Core::IsCooldownFinished() const {
    return std::chrono::steady_clock::now() - *first_cooldown_frame_timestamp_ > std::chrono::milliseconds(settings_.cooldown_write_time_ms);
}

bool Core::IsAlarmImageDelayPassed() const {
    return std::chrono::steady_clock::now() - last_alarm_photo_sent_ > std::chrono::milliseconds(settings_.alarm_notification_delay_ms);
}

void Core::CaptureThreadFunc() {
    while (!stop_) {
        cv::Mat frame;
        if (!frame_reader_.getFrame(frame)) {
            ++get_frame_error_count_;
            LogError() << "Can't get frame";

            if (get_frame_error_count_ >= settings_.errors_before_reconnect) {
                LogInfo() << "Reconnect";
                get_frame_error_count_ = 0;
                frame_reader_.reconnect();
            } else {
                LogInfo() << "Delay after error, error count = " << get_frame_error_count_;
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
            buffer_cv_.notify_all();

            // Useful performance debug output
            static auto debug_buffer_out_time = std::chrono::steady_clock::now();
            if (const auto now = std::chrono::steady_clock::now(); now - debug_buffer_out_time >= std::chrono::seconds(30)) {
                LogDebug() << "Current buffer size = " << buffer_size;
                debug_buffer_out_time = now;
            }

            if (buffer_size > kMaxBufferSize) {
                if (settings_.buffer_overflow_strategy == BufferOverflowStrategy::Delay) {
                    LogWarning() << "Buffer size exceeds max (" << kMaxBufferSize << "), delay capture";
                    std::this_thread::sleep_for(kBufferOverflowDelay);
                } else if (settings_.buffer_overflow_strategy == BufferOverflowStrategy::DropHalf) {
                    LogWarning() << "Buffer size exceeds max (" << kMaxBufferSize << "), dropping half of cache";
                    std::lock_guard lock(buffer_mutex_);
                    const size_t half = buffer_.size() / 2;
                    buffer_.erase(begin(buffer_), begin(buffer_) + static_cast<decltype(buffer_)::difference_type>(half));
                }
            }
        }
    }
    stop_ = true;
    stop_.notify_all();
}

void Core::Start() {
    if (!stop_) {
        LogInfo() << "Attempt start() on already running core";
        return;
    }

    stop_ = false;
    stop_.notify_all();
    capture_thread_ = std::jthread(&Core::CaptureThreadFunc, this);
    processing_thread_ = std::jthread(&Core::ProcessingThreadFunc, this);
}

void Core::Stop() {
    if (stop_) {
        LogInfo() << "Attempt stop() on already stopped core";
    }
    stop_ = true;
    buffer_cv_.notify_all();

    /* Uncomment this if std::thread is used instead of std::jthread
    if (capture_thread_.joinable())
        capture_thread_.join();
    if (processing_thread_.joinable())
        processing_thread_.join();
    */
}
