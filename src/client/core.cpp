#include "core.h"

#include "ai_factory.h"
#include "log.h"
#include "translation.h"
#include "uid_utils.h"
#include "video_writer_factory.h"

constexpr auto kBufferOverflowDelay = std::chrono::seconds(1);
constexpr auto kDecreasedCheckFrameInterval = std::chrono::milliseconds(1000);

Core::Core(Settings settings)
    : settings_(std::move(settings))
    , frame_reader_(settings_.source)
    , bot_(settings_.bot_token, settings_.storage_path, settings_.allowed_users, settings_.admin_users)
    , ai_error_(&bot_, translation::errors::kAiProcessingError, translation::errors::kAiProcessingRestored)
    , frame_reader_error_(&bot_, translation::errors::kGetFrameError, translation::errors::kGetFrameRestored) {

    ai_ = AiFactory(settings_.detection_engine, settings_);
    VideoWriter::kVideoCodec = settings_.video_codec;
    VideoWriter::kVideoFileExtension = "." + settings_.video_container;

    bot_.Start();
    frame_reader_.Open();

    if (settings_.notify_on_start)
        bot_.PostTextMessage(translation::messages::kAppStarted);
}

Core::~Core() {
    bot_.Stop();
    Stop();
}

void Core::PostOnDemandPhoto(const cv::Mat& frame) {
    const auto file_name = GenerateFileName("on_demand_") + ".jpg";
    const auto path = (settings_.storage_path / file_name).generic_string();
    if (!cv::imwrite(path, frame))
        LOG_ERROR_EX << "Error write on-demand photo, " << LOG_VAR(path);
    bot_.PostOnDemandPhoto(path);
}

void Core::InitVideoWriter() {
    LOG_INFO << "Init video writer";
    const auto in_properties = frame_reader_.GetStreamProperties();
    const auto out_properties = StreamProperties{
        in_properties.fps,
        settings_.use_video_scale ? settings_.video_height : in_properties.height,
        settings_.use_video_scale ? settings_.video_width : in_properties.width};
    video_writer_ = VideoWriterFactory(settings_, in_properties, out_properties);
    video_writer_->Start();
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
        LOG_ERROR_EX << "Error write alarm photo, " << LOG_VAR(path);
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
        LOG_ERROR_EX << "Error write video preview image, " << LOG_VAR(path);
    return path;
}

void Core::PostVideoPreview(const std::filesystem::path& file_path) {
    bot_.PostVideoPreview(file_path);
}

void Core::PostVideo(const std::string& uid) {
    const auto file_path = settings_.storage_path / VideoWriter::GenerateVideoFileName(uid);
    bot_.PostVideo(file_path);
}

void Core::ProcessingThreadFunc(std::stop_token stop_token) {
    const auto scaled_size = cv::Size(
        static_cast<int>(frame_reader_.GetStreamProperties().width * settings_.img_scale_x),
        static_cast<int>(frame_reader_.GetStreamProperties().height * settings_.img_scale_y));
    const auto img_format = "." + settings_.img_format;

    cv::Mat frame{};
    cv::Mat scaled_frame{};
    while (!stop_token.stop_requested()) [[unlikely]] {
        std::unique_lock lock(buffer_mutex_);
        buffer_cv_.wait(lock, [&] { return !buffer_.empty() || stop_token.stop_requested(); });

        if (stop_token.stop_requested())
            break;

        frame = std::move(buffer_.front());
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
                LOG_TRACE << "Detect not called, just write";
                video_writer_->AddFrame(frame);
            }
        }

        if (check_frame) {
            last_checked_frame_ = std::chrono::steady_clock::now();

            if (settings_.use_image_scale)
                cv::resize(frame, scaled_frame, scaled_size, cv::INTER_AREA);  // TODO: Check performance

            std::vector<Detection> detections;
            const auto detect_result = ai_->Detect(settings_.use_image_scale ? scaled_frame : frame, detections);
            LOG_TRACE << "Detect result: " << detect_result;
            ai_error_.Update(detect_result ? ErrorReporter::ErrorState::kNoError : ErrorReporter::ErrorState::kError);

            if (detect_result && !detections.empty()) {
                if (first_cooldown_frame_timestamp_) {  // We are writing cooldown sequence, and detected something - stop cooldown
                    LOG_INFO << "Cooldown stopped - object detected";
                    first_cooldown_frame_timestamp_.reset();
                }

                if (!video_writer_)
                    InitVideoWriter();

                video_writer_->AddFrame(frame);
                const auto video_uid = video_writer_->GetUid();
                if (video_uid != last_alarm_video_uid_ || IsAlarmImageDelayPassed()) {
                    auto& alarm_frame = settings_.use_image_scale ? scaled_frame : frame;
                    DrawBoxes(alarm_frame, detections);
                    PostAlarmPhoto(alarm_frame, detections);
                    last_alarm_video_uid_ = video_uid;
                }
            } else {  // Not detected
                if (video_writer_) {
                    video_writer_->AddFrame(std::move(frame));

                    if (!first_cooldown_frame_timestamp_) {
                        LOG_INFO << "Start cooldown writing";
                        first_cooldown_frame_timestamp_ = std::chrono::steady_clock::now();
                    } else {
                        LOG_TRACE << "Cooldown frame saved";
                        if (IsCooldownFinished()) {
                            const auto uid = video_writer_->GetUid();
                            LOG_INFO << "Finish writing file with uid = " << uid;
                            const auto preview_file_path = SaveVideoPreview(uid);
                            if (settings_.send_video_previews)
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

void Core::CaptureThreadFunc(std::stop_token stop_token) {
    cv::Mat frame{};
    while (!stop_token.stop_requested()) [[unlikely]] {
        if (!frame_reader_.GetFrame(frame)) [[unlikely]] {
            ++get_frame_error_count_;
            LOG_ERROR_EX << "Can't get frame";
            frame_reader_error_.Update(ErrorReporter::ErrorState::kError);

            if (get_frame_error_count_ >= settings_.errors_before_reconnect) {
                LOG_INFO << "Reconnect";
                get_frame_error_count_ = 0;
                frame_reader_.Reconnect();
            } else {
                LOG_INFO << "Delay after error, error count = " << get_frame_error_count_;
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
            if (kAppLogLevel <= LogLevel::kDebug) {
                static auto debug_buffer_out_time = std::chrono::steady_clock::now();
                if (const auto now = std::chrono::steady_clock::now(); now - debug_buffer_out_time >= std::chrono::seconds(30)) {
                    LOG_TRACE << "Current buffer size = " << buffer_size;
                    debug_buffer_out_time = now;
                }
            }

            if (buffer_size > settings_.max_buffer_size) {
                if (settings_.buffer_overflow_strategy == BufferOverflowStrategy::kDelay) {
                    LOG_WARNING << "Buffer size exceeds max (" << settings_.max_buffer_size << "), delay capture";
                    std::this_thread::sleep_for(kBufferOverflowDelay);
                } else if (settings_.buffer_overflow_strategy == BufferOverflowStrategy::kDropHalf) {
                    LOG_WARNING << "Buffer size exceeds max (" << settings_.max_buffer_size << "), dropping half of cache";
                    std::lock_guard lock(buffer_mutex_);
                    const size_t half = buffer_.size() / 2;
                    buffer_.erase(begin(buffer_), begin(buffer_) + static_cast<decltype(buffer_)::difference_type>(half));
                }
            }
        }
    }
}

void Core::Start() {
    if (capture_thread_.joinable() || processing_thread_.joinable()) {
        LOG_INFO << "Attempt start() on already running core";
        return;
    }

    capture_thread_ = std::jthread(std::bind_front(&Core::CaptureThreadFunc, this));
    processing_thread_ = std::jthread(std::bind_front(&Core::ProcessingThreadFunc, this));
}

void Core::Stop() {
    const auto capture_stop_requested = capture_thread_.request_stop();
    const auto processing_stop_requested = processing_thread_.request_stop();

    if (!capture_stop_requested || !processing_stop_requested) {
        LOG_INFO << "Attempt stop() on already stopped core. Capture stop request result = " << capture_stop_requested
                 << ", processing stop request result = " << processing_stop_requested;
    }

    buffer_cv_.notify_all();

    // In case this function is not called from within destructor, ensure that threads are stopped
    if (capture_thread_.joinable())
        capture_thread_.join();
    if (processing_thread_.joinable())
        processing_thread_.join();
}
