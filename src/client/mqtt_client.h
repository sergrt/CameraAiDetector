#include <mqtt/async_client.h>

#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

inline const std::string SERVER = "tcp://localhost:1883";
inline const std::string CLIENT_ID = "camera_node_1";

inline const std::string FRAME_TOPIC = "camera/1/frame";
inline const std::string CONTROL_TOPIC = "camera/1/control";

inline std::atomic<bool> running(true);

class Callback :
        public virtual mqtt::callback,
        public virtual mqtt::iaction_listener
{
public:

    void reconnect(mqtt::async_client& client)
    {
        while (running)
        {
            try {
                std::cout << "Reconnecting...\n";
                mqtt::connect_options conn;
                conn.set_clean_session(true);
                client.connect(conn)->wait();
                std::cout << "Reconnected\n";
                return;
            }
            catch (...) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
        }
    }

    void connected(const std::string&) override
    {
        std::cout << "Connected to broker\n";
    }

    void connection_lost(const std::string& cause) override
    {
        std::cout << "Connection lost: " << cause << std::endl;
    }

    void message_arrived(mqtt::const_message_ptr msg) override
    {
        std::string cmd = msg->to_string();

        std::cout << "Command received: " << cmd << std::endl;

        if (cmd == "snapshot")
        {
            std::cout << "Trigger snapshot\n";
        }

        if (cmd == "restart")
        {
            std::cout << "Restart requested\n";
        }
    }

    void delivery_complete(mqtt::delivery_token_ptr tok) override
    {
        if (tok)
            std::cout << "Frame delivered\n";
    }

    void on_failure(const mqtt::token&) override {}
    void on_success(const mqtt::token&) override {}
};

class MqttClient {
public:
    MqttClient()
        : client_(SERVER, CLIENT_ID) {

        client_.set_callback(callback_);
    }

    void Start() {
        mqtt::connect_options conn{};
        conn.set_clean_session(false);
        conn.set_automatic_reconnect(true);
        conn.set_keep_alive_interval(60);

        client_.connect(conn)->wait();

        client_.subscribe(CONTROL_TOPIC, 1)->wait();
    }

    void Stop() {
        running = false;
        client_.disconnect()->wait();
    }

    // Post to sending queue - thread safe
    // user_id is the explicit recipient. If supplied and user is paused - the message still will be sent
    void PostOnDemandPhoto(const std::filesystem::path& file_path);  // No user id - waiting users are stored in 'users_waiting_for_photo_'
    void PostAlarmPhoto(const std::filesystem::path& file_path, const std::string& classes_detected);  // No user id - goes to all users
    void PostTextMessage(const std::string& message, const std::optional<uint64_t>& user_id = std::nullopt);
    void PostVideoPreview(const std::filesystem::path& file_path, const std::optional<uint64_t>& user_id = std::nullopt);
    void PostVideo(const std::filesystem::path& file_path, const std::optional<uint64_t>& user_id = std::nullopt);
    void PostMenu(uint64_t user_id);
    void PostAdminMenu(uint64_t user_id);
    void PostAnswerCallback(const std::string& callback_id);
    
    void PostVideo(std)
    {
        auto msg = mqtt::make_message(FRAME_TOPIC,
                                      frame.data(),
                                      frame.size());

        msg->set_qos(1);
        msg->set_retained(false);

        client_.publish(msg);
    }

    

    bool SomeoneIsWaitingForPhoto() const {
        return false;
    }

private:
    mqtt::async_client client_;
    Callback callback_;
};
