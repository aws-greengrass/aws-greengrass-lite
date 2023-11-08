#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/crt/mqtt/Mqtt5Types.h>
#include <aws/iot/Mqtt5Client.h>
#include <iostream>
#include <memory>
#include <plugin.hpp>
#include <shared_mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>

struct Keys {
    ggapi::StringOrd publishToIoTCoreTopic{"aws.greengrass.PublishToIoTCore"};
    ggapi::StringOrd subscribeToIoTCoreTopic{"aws.greengrass.SubscribeToIoTCore"};
    ggapi::StringOrd topicName{"topicName"};
    ggapi::StringOrd topicFilter{"topicFilter"};
    ggapi::StringOrd qos{"qos"};
    ggapi::StringOrd payload{"payload"};
    ggapi::StringOrd lpcResponseTopic{"lpcResponseTopic"};
};

struct TopicLevelIterator {
    using value_type = std::string_view;
    using difference_type = std::ptrdiff_t;
    using reference = const value_type &;
    using pointer = const value_type *;
    using iterator_category = std::input_iterator_tag;

    explicit TopicLevelIterator(std::string_view topic) noexcept
        : _str(topic), _index(0), _current(_str.substr(_index, _str.find('/', _index))) {
    }

    friend bool operator==(const TopicLevelIterator &a, const TopicLevelIterator &b) noexcept {
        return (a._index == b._index) && (a._str.data() == b._str.data());
    }

    friend bool operator!=(const TopicLevelIterator &a, const TopicLevelIterator &b) noexcept {
        return (a._index != b._index) || (a._str.data() != b._str.data());
    }

    reference operator*() const {
        if(_index == value_type::npos) {
            throw std::out_of_range{"Using depleted TopicLevelIterator."};
        }
        return _current;
    }

    pointer operator->() const {
        if(_index == value_type::npos) {
            throw std::out_of_range{"Using depleted TopicLevelIterator."};
        }
        return &_current;
    }

    TopicLevelIterator &operator++() {
        if(_index == value_type::npos) {
            throw std::out_of_range{"Using depleted TopicLevelIterator."};
        }
        _index += _current.length() + 1;
        if(_index > _str.length()) {
            _index = value_type::npos;
        } else {
            size_t nextIndex = _str.find('/', _index);
            if(nextIndex == value_type::npos) {
                nextIndex = _str.length();
            }
            _current = _str.substr(_index, nextIndex - _index);
        }
        return *this;
    }

    // NOLINTNEXTLINE(readability-const-return-type) Conflicting lints.
    const TopicLevelIterator operator++(int) {
        TopicLevelIterator tmp = *this;
        ++*this;
        return tmp;
    }

    [[nodiscard]] TopicLevelIterator begin() const {
        return *this;
    }

    [[nodiscard]] TopicLevelIterator end() const {
        return TopicLevelIterator{this->_str, value_type::npos};
    }

private:
    explicit TopicLevelIterator(std::string_view topic, size_t index) : _str(topic), _index(index) {
    }

    std::string_view _str;
    size_t _index;
    value_type _current;
};

class TopicFilter {
public:
    using const_iterator = TopicLevelIterator;

    explicit TopicFilter(std::string_view str) : _value{str} {
        validateFilter();
    }

    explicit TopicFilter(std::string &&str) : _value{std::move(str)} {
        validateFilter();
    }

    TopicFilter(const TopicFilter &) = default;
    TopicFilter(TopicFilter &&) noexcept = default;
    TopicFilter &operator=(const TopicFilter &) = default;
    TopicFilter &operator=(TopicFilter &&) noexcept = default;
    ~TopicFilter() noexcept = default;

    explicit operator const std::string &() const noexcept {
        return _value;
    }

    friend bool operator==(const TopicFilter &a, const TopicFilter &b) noexcept {
        return a._value == b._value;
    }

    [[nodiscard]] bool match(std::string_view topic) const noexcept {
        TopicLevelIterator topicIter{topic};
        bool hash = false;
        auto [filterTail, topicTail] = std::mismatch(
            begin(),
            end(),
            topicIter.begin(),
            topicIter.end(),
            [&hash](std::string_view filterLevel, std::string_view topicLevel) {
                if(filterLevel == "#") {
                    hash = true;
                    return true;
                }
                return (filterLevel == "+") || (filterLevel == topicLevel);
            }
        );
        return hash || ((filterTail == end()) && (topicTail == topicIter.end()));
    }

    struct Hash {
        size_t operator()(const TopicFilter &filter) const noexcept {
            return std::hash<std::string>{}(filter._value);
        }
    };

    [[nodiscard]] const_iterator begin() const {
        return TopicLevelIterator(_value).begin();
    }

    [[nodiscard]] const_iterator end() const {
        return TopicLevelIterator(_value).end();
    }

    [[nodiscard]] const std::string &get() const noexcept {
        return _value;
    }

private:
    std::string _value;

    void validateFilter() const {
        if(_value.empty()) {
            throw std::invalid_argument("Invalid topic filter");
        }
        bool last = false;
        for(auto level : *this) {
            if(last
               || ((level != "#") && (level != "+")
                   && (level.find_first_of("#+") != std::string_view::npos))) {
                throw std::invalid_argument("Invalid topic filter");
            }
            if(level == "#") {
                last = true;
            }
        }
    }
};

class IotBroker : public ggapi::Plugin {
public:
    bool onStart(ggapi::Struct data) override;
    void beforeLifecycle(ggapi::StringOrd phase, ggapi::Struct data) override;

    static IotBroker &get() {
        static IotBroker instance{};
        return instance;
    }

private:
    static const Keys keys;
    std::unordered_multimap<TopicFilter, ggapi::StringOrd, TopicFilter::Hash> _subscriptions;
    std::shared_mutex _subscriptionMutex;
    std::shared_ptr<Aws::Crt::Mqtt5::Mqtt5Client> _client;
    static ggapi::Struct publishHandler(ggapi::Task, ggapi::StringOrd, ggapi::Struct args);
    ggapi::Struct publishHandlerImpl(ggapi::Struct args);
    static ggapi::Struct subscribeHandler(ggapi::Task, ggapi::StringOrd, ggapi::Struct args);
    ggapi::Struct subscribeHandlerImpl(ggapi::Struct args);
};

const Keys IotBroker::keys{};

// Initializes global CRT API
// TODO: What happens when multiple plugins use the CRT?
static const Aws::Crt::ApiHandle apiHandle;

ggapi::Struct IotBroker::publishHandler(ggapi::Task, ggapi::StringOrd, ggapi::Struct args) {
    return get().publishHandlerImpl(args);
}

ggapi::Struct IotBroker::publishHandlerImpl(ggapi::Struct args) {
    auto topic{args.get<std::string>(keys.topicName)};
    auto qos{args.get<int>(keys.qos)};
    auto payload{args.get<std::string>(keys.payload)};

    std::cerr << "[mqtt-plugin] Sending " << payload << " to " << topic << std::endl;

    auto onPublishComplete = [](int,
                                const std::shared_ptr<Aws::Crt::Mqtt5::PublishResult> &result) {
        if(!result->wasSuccessful()) {
            std::cerr << "[mqtt-plugin] Publish failed with error_code: " << result->getErrorCode()
                      << std::endl;
            return;
        }

        auto puback = std::dynamic_pointer_cast<Aws::Crt::Mqtt5::PubAckPacket>(result->getAck());

        if(puback) {

            if(puback->getReasonCode() == 0) {
                std::cerr << "[mqtt-plugin] Puback success" << std::endl;
            } else {
                std::cerr << "[mqtt-plugin] Puback failed: " << puback->getReasonString().value()
                          << std::endl;
            }
        }
    };

    auto publish = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
        Aws::Crt::String(topic),
        ByteCursorFromString(Aws::Crt::String(payload)),
        static_cast<Aws::Crt::Mqtt5::QOS>(qos)
    );

    if(!_client->Publish(publish, onPublishComplete)) {
        std::cerr << "[mqtt-plugin] Publish failed" << std::endl;
    }

    return ggapi::Struct::create();
}

ggapi::Struct IotBroker::subscribeHandler(ggapi::Task, ggapi::StringOrd, ggapi::Struct args) {
    return get().subscribeHandlerImpl(args);
}

ggapi::Struct IotBroker::subscribeHandlerImpl(ggapi::Struct args) {
    TopicFilter topicFilter{args.get<std::string>(keys.topicFilter)};
    int qos{args.get<int>(keys.qos)};
    ggapi::StringOrd responseTopic{args.get<std::string>(keys.lpcResponseTopic)};

    std::cerr << "[mqtt-plugin] Subscribing to " << topicFilter.get() << std::endl;

    auto onSubscribeComplete = [this, topicFilter, responseTopic](
                                   int error_code,
                                   const std::shared_ptr<Aws::Crt::Mqtt5::SubAckPacket> &suback
                               ) {
        if(error_code != 0) {
            std::cerr << "[mqtt-plugin] Subscribe failed with error_code: " << error_code
                      << std::endl;
            return;
        }

        if(suback && !suback->getReasonCodes().empty()) {
            auto reasonCode = suback->getReasonCodes()[0];
            if(reasonCode >= Aws::Crt::Mqtt5::SubAckReasonCode::AWS_MQTT5_SARC_UNSPECIFIED_ERROR) {
                std::cerr << "[mqtt-plugin] Subscribe rejected with reason code: " << reasonCode
                          << std::endl;
                return;
            } else {
                std::cerr << "[mqtt-plugin] Subscribe accepted" << std::endl;
            }
        };

        {
            std::unique_lock lock(_subscriptionMutex);
            _subscriptions.insert({topicFilter, responseTopic});
        }
    };

    auto subscribe = std::make_shared<Aws::Crt::Mqtt5::SubscribePacket>();
    subscribe->WithSubscription(std::move(Aws::Crt::Mqtt5::Subscription(
        Aws::Crt::String(topicFilter.get()), static_cast<Aws::Crt::Mqtt5::QOS>(qos)
    )));

    if(!_client->Subscribe(subscribe, onSubscribeComplete)) {
        std::cerr << "[mqtt-plugin] Subscribe failed" << std::endl;
    }

    return ggapi::Struct::create();
}

extern "C" [[maybe_unused]] bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t dataHandle
) noexcept {
    return IotBroker::get().lifecycle(moduleHandle, phase, dataHandle);
}

static std::ostream &operator<<(std::ostream &os, Aws::Crt::ByteCursor bc) {
    for(int byte : std::basic_string_view<uint8_t>(bc.ptr, bc.len)) {
        if(isprint(byte)) {
            os << static_cast<char>(byte);
        } else {
            os << '\\' << byte;
        }
    }
    return os;
}

void IotBroker::beforeLifecycle(ggapi::StringOrd phase, ggapi::Struct data) {
    std::cerr << "[mqtt-plugin] Running lifecycle phase " << phase.toString() << std::endl;
}

bool IotBroker::onStart(ggapi::Struct structData) {
    auto configStruct = structData.getValue<ggapi::Struct>({"config"});
    auto certificateFilePath =
        configStruct.getValue<std::string>({"system", "certificateFilePath"});
    auto privateKeyPath = configStruct.getValue<std::string>({"system", "privateKeyPath"});
    auto credEndpoint = configStruct.getValue<std::string>(
        {"services", "aws.greengrass.Nucleus-Lite", "configuration", "iotCredEndpoint"}
    );

    std::promise<bool> connectionPromise;

    {
        Aws::Crt::String crtEndpoint{credEndpoint};
        std::unique_ptr<Aws::Iot::Mqtt5ClientBuilder> builder{
            Aws::Iot::Mqtt5ClientBuilder::NewMqtt5ClientBuilderWithMtlsFromPath(
                crtEndpoint, certificateFilePath.c_str(), privateKeyPath.c_str()
            )};

        if(!builder) {
            std::cerr << "[mqtt-plugin] Failed to set up MQTT client builder." << std::endl;
            return false;
        }

        auto connectOptions = std::make_shared<Aws::Crt::Mqtt5::ConnectPacket>();
        connectOptions->WithClientId("gglite-test");
        builder->WithConnectOptions(connectOptions);

        builder->WithClientConnectionSuccessCallback(
            [&connectionPromise](const Aws::Crt::Mqtt5::OnConnectionSuccessEventData &eventData) {
                std::cerr << "[mqtt-plugin] Connection successful with clientid "
                          << eventData.negotiatedSettings->getClientId() << "." << std::endl;
                connectionPromise.set_value(true);
            }
        );

        builder->WithClientConnectionFailureCallback(
            [&connectionPromise](const Aws::Crt::Mqtt5::OnConnectionFailureEventData &eventData) {
                std::cerr << "[mqtt-plugin] Connection failed: "
                          << aws_error_debug_str(eventData.errorCode) << "." << std::endl;
                connectionPromise.set_value(false);
            }
        );

        builder->WithPublishReceivedCallback(
            [this](const Aws::Crt::Mqtt5::PublishReceivedEventData &eventData) {
                if(!eventData.publishPacket) {
                    return;
                }

                std::string topic{eventData.publishPacket->getTopic()};
                std::string payload{
                    reinterpret_cast<char *>(eventData.publishPacket->getPayload().ptr),
                    eventData.publishPacket->getPayload().len};

                std::cerr << "[mqtt-plugin] Publish recieved on topic " << topic << ": " << payload
                          << std::endl;

                auto response{ggapi::Struct::create()};
                response.put(keys.topicName, topic);
                response.put(keys.payload, payload);

                {
                    std::shared_lock lock(_subscriptionMutex);
                    for(const auto &[key, value] : _subscriptions) {
                        if(key.match(topic)) {
                            (void) ggapi::Task::sendToTopic(value, response);
                        }
                    }
                }
            }
        );

        _client = builder->Build();
    }

    if(!_client) {
        std::cerr << "[mqtt-plugin] Failed to init MQTT client: "
                  << Aws::Crt::ErrorDebugString(Aws::Crt::LastError()) << "." << std::endl;
        return false;
    }

    if(!_client->Start()) {
        std::cerr << "[mqtt-plugin] Failed to start MQTT client." << std::endl;
        return false;
    }

    if(!connectionPromise.get_future().get()) {
        return false;
    }

    (void) getScope().subscribeToTopic(keys.publishToIoTCoreTopic, publishHandler);
    (void) getScope().subscribeToTopic(keys.subscribeToIoTCoreTopic, subscribeHandler);

    return true;
}
