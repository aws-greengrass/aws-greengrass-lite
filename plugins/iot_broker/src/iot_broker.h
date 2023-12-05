//
// Created by Julicher, Joe on 12/5/23.
//

#ifndef GREENGRASS_LITE_IOT_BROKER_H
#define GREENGRASS_LITE_IOT_BROKER_H

class IotBroker : public ggapi::Plugin {
    struct ThingInfo _thingInfo;
    std::thread _asyncThread;
    std::atomic<ggapi::Struct> _nucleus;
    std::atomic<ggapi::Struct> _system;

public:
    bool onBootstrap(ggapi::Struct data) override;
    bool onBind(ggapi::Struct data) override;
    bool onDiscover(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
    bool onRun(ggapi::Struct data) override;
    bool onTerminate(ggapi::Struct data) override;
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;
    bool validConfig() const;
    bool initMqtt();
    void publishToProvisionPlugin();

    static IotBroker &get() {
        static IotBroker instance{};
        return instance;
    }

private:
    static const Keys keys;
    std::unordered_multimap<TopicFilter, ggapi::Symbol, TopicFilter::Hash> _subscriptions;
    std::shared_mutex _subscriptionMutex;
    std::shared_ptr<Aws::Crt::Mqtt5::Mqtt5Client> _client;
    static ggapi::Struct publishHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct args);
    ggapi::Struct publishHandlerImpl(ggapi::Struct args);
    static ggapi::Struct subscribeHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct args);
    ggapi::Struct subscribeHandlerImpl(ggapi::Struct args);
};


#endif // GREENGRASS_LITE_IOT_BROKER_H
