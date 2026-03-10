#include <mqtt/async_client.h>
#include <iostream>

const std::string SERVER_ADDRESS = "tcp://localhost:1883";
const std::string CLIENT_ID = "telegram_sender";
const std::string TOPIC = "camera/images";

class callback : public virtual mqtt::callback
{
public:
    void message_arrived(mqtt::const_message_ptr msg) override
    {
        std::cout << "Received image: "
                  << msg->get_payload().size()
                  << " bytes" << std::endl;

        // Here you would send to Telegram
        // send_to_telegram(msg->get_payload());
    }
};

std::mutex mtx;
std::condition_variable cv;
bool running = true;

int main()
{
    mqtt::async_client client(SERVER_ADDRESS, CLIENT_ID);
    callback cb;

    client.set_callback(cb);

    mqtt::connect_options conn;
    conn.set_clean_session(false);
    conn.set_keep_alive_interval(60);
    conn.set_automatic_reconnect(true);
    
    //conn.set_ssl(mqtt::ssl_options{})

    client.connect(conn)->wait(); // wait for
    auto token = client.subscribe(TOPIC, 1);
    token->wait(); // wait for

    std::cout << "Waiting for messages...\n";

    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, []{ return !running; });

    client.disconnect()->wait();
}