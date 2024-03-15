#include "core.h"

#include "ai_factory.h"
#include "log.h"
#include "translation.h"
#include "uid_utils.h"

constexpr auto kBufferOverflowDelay = std::chrono::seconds(1);
constexpr auto kDecreasedCheckFrameInterval = std::chrono::milliseconds(1000);

Core::Core(Settings settings)
    : settings_(std::move(settings))
    , frame_reader_(settings_.source)
    , bot_(settings_.bot_token, settings_.storage_path, settings_.allowed_users)
    , ai_error_(&bot_, translation::errors::kAiProcessingError, translation::errors::kAiProcessingRestored)
    , frame_reader_error_(&bot_, translation::errors::kGetFrameError, translation::errors::kGetFrameRestored) {

    ai_ = AiFactory(settings_.detection_engine, settings_);
    VideoWriter::kVideoCodec = settings_.video_codec;
    VideoWriter::kVideoFileExtension = "." + settings_.video_container;

    bot_.Start();
    frame_reader_.Open();

    if (settings_.notify_on_start)
        bot_.PostMessage(translation::messages::kAppStarted);
}

Core::~Core() {
    bot_.Stop();
    Stop();
}

void Core::PostOnDemandPhoto(const cv::Mat& frame) {
    const auto file_name = GenerateFileName("on_demand_") + ".jpg";
    const auto path = (settings_.storage_path / file_name).generic_string();
    if (!cv::imwrite(path, frame))
        LogError() << "Error write on-demand photo, path = " << path;
    bot_.PostOnDemandPhoto(path);
}

void Core::InitVideoWriter() {
    LogInfo() << "Init video writer";
    const auto in_properties = frame_reader_.GetStreamProperties();
    const auto out_properties = StreamProperties{
        in_properties.fps,
        settings_.use_video_scale ? settings_.video_height : in_properties.height,
        settings_.use_video_scale ? settings_.video_width : in_properties.width};
    video_writer_ = std::make_unique<VideoWriter>(settings_, in_properties, out_properties);
}

void Core::PostAlarmPhoto(const cv::Mat& frame, const std::vector<Detection>& detections) {
    last_alarm_photo_sent_ = std::chrono::steady_clock::now();

    std::string classes_detected;
    for (const auto& detection : detections) {
        classes_detected += detection.class_name + ", ";
    }
    if (!classes_detected.empty()) {
        classes_detected.erase(classes_detected.size() - 2, 2);  // last ", "
    }


    const auto file_name = GenerateFileName("alarm_") + ".jpg";
    const auto path = settings_.storage_path / file_name;
    if (!cv::imwrite(path.generic_string(), frame))
        LogError() << "Error write alarm photo, path = " << path;
    bot_.PostAlarmPhoto(path, classes_detected);
} 

void Core::DrawBoxes(const cv::Mat& frame, const std::vector<Detection>& detections) {
    static const cv::Scalar frame_color = {settings_.frame_color.R, settings_.frame_color.G, settings_.frame_color.B};
    for (const auto& detection : detections) {
        cv::rectangle(frame, detection.box, frame_color, settings_.frame_width_px);
    }
}

std::filesystem::path Core::SaveVideoPreview(const std::string& video_file_uid) {
    const auto file_name = VideoWriter::GeneratePreviewFileName(video_file_uid);
    const std::vector<int> img_encode_param{cv::IMWRITE_JPEG_QUALITY, 90};
    auto path = settings_.storage_path / file_name;
    if (!cv::imwrite(path.generic_string(), video_writer_->GetPreviewImage(), img_encode_param))
        LogError() << "Error write video preview image, path = " << path;
    return path;
}

void Core::PostVideoPreview(const std::filesystem::path& file_path) {
    bot_.PostVideoPreview(file_path);
}

void Core::PostVideo(const std::string& uid) {
    const auto file_path = settings_.storage_path / VideoWriter::GenerateVideoFileName(uid);
    bot_.PostVideo(file_path);
}

void Core::ProcessingThreadFunc() {
    const auto scaled_size = cv::Size(
        static_cast<int>(frame_reader_.GetStreamProperties().width * settings_.img_scale_x),
        static_cast<int>(frame_reader_.GetStreamProperties().height * settings_.img_scale_y));
    const auto img_format = "." + settings_.img_format;

    while (!stop_) {
        std::unique_lock lock(buffer_mutex_);
        buffer_cv_.wait(lock, [&] { return !buffer_.empty() || stop_; });

        if (stop_)
            break;

        const cv::Mat frame = std::move(buffer_.front());
        buffer_.pop_front();
        //cv::Mat frame = buffer_.front();
        lock.unlock();

        if (bot_.SomeoneIsWaitingForPhoto())
            PostOnDemandPhoto(frame);

        static uint64_t i = 0;
        bool check_frame = (i++ % settings_.nth_detect_frame == 0);

        // Check if decreased check rate is used and alter check_frame if needed
        if (check_frame && video_writer_
            && settings_.decrease_detect_rate_while_writing
            && std::chrono::steady_clock::now() - last_checked_frame_ < kDecreasedCheckFrameInterval) {

            check_frame = false;
        }

        if (!check_frame) {
            if (video_writer_) {
                LogTrace() << "Detect not called, just write";
                video_writer_->Write(frame);
            }
        }

        if (check_frame) {
            last_checked_frame_ = std::chrono::steady_clock::now();
            cv::Mat scaled_frame;
            if (settings_.use_image_scale)
                cv::resize(frame, scaled_frame, scaled_size, cv::INTER_AREA);  // TODO: Check performance

            std::vector<Detection> detections;
            const auto detect_result = ai_->Detect(settings_.use_image_scale ? scaled_frame : frame, detections);
            LogTrace() << "Detect result: " << detect_result;
            ai_error_.Update(detect_result ? ErrorReporter::ErrorState::kNoError : ErrorReporter::ErrorState::kError);

            if (detect_result && !detections.empty()) {
                if (first_cooldown_frame_timestamp_) {  // We are writing cooldown sequence, and detected something - stop cooldown
                    LogInfo() << "Cooldown stopped - object detected";
                    first_cooldown_frame_timestamp_.reset();
                }

                if (!video_writer_)
                    InitVideoWriter();

                video_writer_->Write(frame);
                const auto video_uid = video_writer_->GetUid();
                if (video_uid != last_alarm_video_uid_ || IsAlarmImageDelayPassed()) {
                    auto& alarm_frame = settings_.use_image_scale ? scaled_frame : frame;
                    DrawBoxes(alarm_frame, detections);
                    PostAlarmPhoto(alarm_frame, detections);
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
                            if (settings_.send_video)
                                PostVideo(uid);
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
        if (!frame_reader_.GetFrame(frame)) {
            ++get_frame_error_count_;
            LogError() << "Can't get frame";
            frame_reader_error_.Update(ErrorReporter::ErrorState::kError);

            if (get_frame_error_count_ >= settings_.errors_before_reconnect) {
                LogInfo() << "Reconnect";
                get_frame_error_count_ = 0;
                frame_reader_.Reconnect();
            } else {
                LogInfo() << "Delay after error, error count = " << get_frame_error_count_;
                std::this_thread::sleep_for(std::chrono::milliseconds(settings_.delay_after_error_ms));
            }
        } else {
            frame_reader_error_.Update(ErrorReporter::ErrorState::kNoError);
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

            if (buffer_size > settings_.max_buffer_size) {
                if (settings_.buffer_overflow_strategy == BufferOverflowStrategy::kDelay) {
                    LogWarning() << "Buffer size exceeds max (" << settings_.max_buffer_size << "), delay capture";
                    std::this_thread::sleep_for(kBufferOverflowDelay);
                } else if (settings_.buffer_overflow_strategy == BufferOverflowStrategy::kDropHalf) {
                    LogWarning() << "Buffer size exceeds max (" << settings_.max_buffer_size << "), dropping half of cache";
                    std::lock_guard lock(buffer_mutex_);
                    const size_t half = buffer_.size() / 2;
                    buffer_.erase(begin(buffer_), begin(buffer_) + static_cast<decltype(buffer_)::difference_type>(half));
                }
            }
        }
    }
    stop_ = true;
}

void Core::Start() {
    if (!stop_) {
        LogInfo() << "Attempt start() on already running core";
        return;
    }

    stop_ = false;
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
