#include <plugin.hpp>
#include <thread>

struct Keys {
    ggapi::Symbol publishToIoTCoreTopic{"aws.greengrass.PublishToIoTCore"};
    ggapi::Symbol subscribeToIoTCoreTopic{"aws.greengrass.SubscribeToIoTCore"};
    ggapi::Symbol topicName{"topicName"};
    ggapi::Symbol topicFilter{"topicFilter"};
    ggapi::Symbol qos{"qos"};
    ggapi::Symbol payload{"payload"};
    ggapi::Symbol mqttPing{"mqttPing"};
    ggapi::Symbol lpcResponseTopic{"lpcResponseTopic"};
};

class ExampleMqttSender : public ggapi::Plugin {
    std::atomic_bool _running{true};
    void threadFn();
    ggapi::Struct mqttListener(ggapi::Task task, ggapi::Symbol, ggapi::Struct args);

protected:
    std::atomic<ggapi::Struct> _publishMessage{};
    ggapi::Struct _subscribeMessage{ggapi::Struct::create()};

public:
    bool onStart(ggapi::Struct data) override;
    bool onRun(ggapi::Struct data) override;
    bool onTerminate(ggapi::Struct data) override;
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;
    static ExampleMqttSender &get() {
        static ExampleMqttSender instance{};
        return instance;
    }
};
