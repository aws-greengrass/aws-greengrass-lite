#include "plugin.hpp"
#include <condition_variable>
#include <thread>

struct Keys {
    ggapi::Symbol publishToIoTCoreTopic{"aws.greengrass.PublishToIoTCore"};
    ggapi::Symbol subscribeToIoTCoreTopic{"aws.greengrass.SubscribeToIoTCore"};
    ggapi::Symbol topicName{"topicName"};
    ggapi::Symbol qos{"qos"};
    ggapi::Symbol payload{"payload"};
    ggapi::Symbol mqttPing{"mqttPing"};
    ggapi::StringOrd channel{"channel"};
};

class MqttSender : public ggapi::Plugin {
    void threadFn();
    std::thread _asyncThread;

protected:
    std::atomic_bool _running{false};
    std::mutex _mtx;
    std::condition_variable _cv;

public:
    bool onStart(ggapi::Struct data) override;
    bool onRun(ggapi::Struct data) override;
    bool onTerminate(ggapi::Struct data) override;
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;
    static MqttSender &get() {
        static MqttSender instance{};
        return instance;
    }
};

static std::ostream &operator<<(std::ostream &os, const ggapi::Buffer &buffer) {
    auto buf = buffer.get<std::string>(0, 1000);
    return os << buf;
}
