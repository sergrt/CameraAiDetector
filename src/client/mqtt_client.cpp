#include "mqtt_client.h"

void MqttClient::PostOnDemandPhoto(const std::filesystem::path& file_path) {

}
void PostAlarmPhoto(const std::filesystem::path& file_path, const std::string& classes_detected);  // No user id - goes to all users
void PostTextMessage(const std::string& message, const std::optional<uint64_t>& user_id = std::nullopt);
void PostVideoPreview(const std::filesystem::path& file_path, const std::optional<uint64_t>& user_id = std::nullopt);
void PostVideo(const std::filesystem::path& file_path, const std::optional<uint64_t>& user_id = std::nullopt);
void PostMenu(uint64_t user_id);
void PostAdminMenu(uint64_t user_id);
void PostAnswerCallback(const std::string& callback_id);