#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
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
    ggapi::Symbol publishToIoTCoreTopic{"aws.greengrass.PublishToIoTCore"};
    ggapi::Symbol subscribeToIoTCoreTopic{"aws.greengrass.SubscribeToIoTCore"};
    ggapi::Symbol requestDeviceProvisionTopic{"aws.greengrass.RequestDeviceProvision"};
    // TODO: This needs to be encapsulated within provisioning_plugin
    ggapi::Symbol fleetProvisioningByClaimService{"aws.greengrass.FleetProvisioningByClaim"};
    ggapi::Symbol topicName{"topicName"};
    ggapi::Symbol topicFilter{"topicFilter"};
    ggapi::Symbol qos{"qos"};
    ggapi::Symbol payload{"payload"};
    ggapi::Symbol lpcResponseTopic{"lpcResponseTopic"};
};

struct ThingInfo {
    std::string thingName;
    std::string credEndpoint;
    std::string dataEndpoint;
    std::string certPath;
    std::string keyPath;
    std::string rootCaPath;
    std::string rootPath;
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
            });
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
    struct ThingInfo _thingInfo;
    std::thread _asyncThread;
    std::atomic<ggapi::Struct> _nucleus;
    std::atomic<ggapi::Struct> _system;

public:
    bool onBootstrap(ggapi::Struct data) override;
    bool onBind(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
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

const Keys IotBroker::keys{};

// Initializes global CRT API
// TODO: What happens when multiple plugins use the CRT?
static Aws::Crt::ApiHandle apiHandle{};

ggapi::Struct IotBroker::publishHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct args) {
    std::cerr << "[mqtt-plugin] received a publish request" << std::endl;
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
        static_cast<Aws::Crt::Mqtt5::QOS>(qos));

    if(!_client->Publish(publish, onPublishComplete)) {
        std::cerr << "[mqtt-plugin] Publish failed" << std::endl;
    }

    return ggapi::Struct::create();
}

ggapi::Struct IotBroker::subscribeHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct args) {
    return get().subscribeHandlerImpl(args);
}

ggapi::Struct IotBroker::subscribeHandlerImpl(ggapi::Struct args) {
    TopicFilter topicFilter{args.get<std::string>(keys.topicFilter)};
    int qos{args.get<int>(keys.qos)};
    ggapi::Symbol responseTopic{args.get<std::string>(keys.lpcResponseTopic)};

    std::cerr << "[mqtt-plugin] Subscribing to " << topicFilter.get() << std::endl;

    auto onSubscribeComplete = [this, topicFilter, responseTopic](
                                   int error_code,
                                   const std::shared_ptr<Aws::Crt::Mqtt5::SubAckPacket> &suback) {
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
        }

        {
            std::unique_lock lock(_subscriptionMutex);
            _subscriptions.insert({topicFilter, responseTopic});
        }
    };

    auto subscribe = std::make_shared<Aws::Crt::Mqtt5::SubscribePacket>();
    subscribe->WithSubscription(std::move(Aws::Crt::Mqtt5::Subscription(
        Aws::Crt::String(topicFilter.get()), static_cast<Aws::Crt::Mqtt5::QOS>(qos))));

    if(!_client->Subscribe(subscribe, onSubscribeComplete)) {
        std::cerr << "[mqtt-plugin] Subscribe failed" << std::endl;
    }

    return ggapi::Struct::create();
}

extern "C" [[maybe_unused]] bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t dataHandle) noexcept {
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

void IotBroker::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    std::cerr << "[mqtt-plugin] Running lifecycle phase " << phase.toString() << std::endl;
}

bool IotBroker::onBootstrap(ggapi::Struct structData) {
    structData.put("name", "aws.greengrass.iot_broker");
    std::cout << "[mqtt-plugin] bootstrapping\n";
    apiHandle.InitializeLogging(Aws::Crt::LogLevel::Debug, stderr);
    return true;
}

bool IotBroker::onTerminate(ggapi::Struct structData) {
    // TODO: Cleanly stop thread and clean up listeners
    std::cout << "[mqtt-plugin] terminating\n";
    return true;
}

bool IotBroker::onBind(ggapi::Struct data) {
    _nucleus = getScope().anchor(data.getValue<ggapi::Struct>({"nucleus"}));
    _system = getScope().anchor(data.getValue<ggapi::Struct>({"system"}));
    std::cout << "[mqtt-plugin] binding\n";
    return true;
}

bool IotBroker::onStart(ggapi::Struct data) {
    std::cout << "[mqtt-plugin] starting\n";

    auto service = getConfig();
    auto nucleus = _nucleus.load();
    auto system = _system.load();

    _thingInfo.certPath = system.getValue<std::string>({"certificateFilePath"});
    _thingInfo.keyPath = system.getValue<std::string>({"privateKeyPath"});
    _thingInfo.rootPath = system.getValue<std::string>({"rootpath"});
    _thingInfo.rootCaPath = system.getValue<std::string>({"rootCaPath"});
    _thingInfo.thingName = system.getValue<std::string>({"thingName"});

    // TODO: Note, reference of the module name will be done by Nucleus, this is temporary.
    _thingInfo.credEndpoint = nucleus.getValue<std::string>({"configuration", "iotCredEndpoint"});
    _thingInfo.dataEndpoint = nucleus.getValue<std::string>({"configuration", "iotDataEndpoint"});

    if(!validConfig()) {
        std::cout << "[mqtt-plugin] Device is not provisioned. Running provision plugin...\n";
        try {
            publishToProvisionPlugin();
        } catch(const std::exception &e) {
            std::cerr << "[mqtt-plugin] Error while running the provision plugin \n." << e.what();
            return false;
        }
    }
    if(initMqtt()) {
        std::ignore = getScope().subscribeToTopic(keys.publishToIoTCoreTopic, publishHandler);
        std::ignore = getScope().subscribeToTopic(keys.subscribeToIoTCoreTopic, subscribeHandler);
        return true;
    }
    std::cerr << "[mqtt-plugin] initMqtt returned false." << std::endl;
    return false;
}

bool inline IotBroker::validConfig() const {
    if(_thingInfo.certPath.empty() || _thingInfo.keyPath.empty() || _thingInfo.thingName.empty()) {
        return false;
    }
    return true;
}

bool IotBroker::initMqtt() {
    std::cerr << "[mqtt-plugin] initMqtt." << std::endl;

    {
        Aws::Crt::String crtEndpoint{_thingInfo.dataEndpoint};
        std::unique_ptr<Aws::Iot::Mqtt5ClientBuilder> builder{
            Aws::Iot::Mqtt5ClientBuilder::NewMqtt5ClientBuilderWithMtlsFromPath(
                crtEndpoint, _thingInfo.certPath.c_str(), _thingInfo.keyPath.c_str())};

        if(!builder) {
            std::cerr << "[mqtt-plugin] Failed to set up MQTT client builder." << std::endl;
            return false;
        }

        auto connectOptions = std::make_shared<Aws::Crt::Mqtt5::ConnectPacket>();
        connectOptions->WithClientId(
            _thingInfo.thingName.c_str()); // NOLINT(*-redundant-string-cstr)
        builder->WithConnectOptions(connectOptions);

        builder->WithClientConnectionSuccessCallback(
            [](const Aws::Crt::Mqtt5::OnConnectionSuccessEventData &eventData) {
                std::cerr << "[mqtt-plugin] Connection successful with clientid "
                          << eventData.negotiatedSettings->getClientId() << "." << std::endl;
            });

        builder->WithClientConnectionFailureCallback(
            [](const Aws::Crt::Mqtt5::OnConnectionFailureEventData &eventData) {
                std::cerr << "[mqtt-plugin] Connection failed: "
                          << aws_error_debug_str(eventData.errorCode) << "." << std::endl;
            });

        builder->WithPublishReceivedCallback(
            [this](const Aws::Crt::Mqtt5::PublishReceivedEventData &eventData) {
                if(!eventData.publishPacket) {
                    return;
                }

                std::string topic{eventData.publishPacket->getTopic()};
                std::string payload{
                    reinterpret_cast<char *>(eventData.publishPacket->getPayload().ptr),
                    eventData.publishPacket->getPayload().len};

                std::cerr << "[mqtt-plugin] Publish received on topic " << topic << ": " << payload
                          << std::endl;

                auto response{ggapi::Struct::create()};
                response.put(keys.topicName, topic);
                response.put(keys.payload, payload);

                {
                    std::shared_lock lock(_subscriptionMutex);
                    for(const auto &[key, value] : _subscriptions) {
                        if(key.match(topic)) {
                            std::ignore = ggapi::Task::sendToTopic(value, response);
                        }
                    }
                }
            });

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

    return true;
}

void IotBroker::publishToProvisionPlugin() {
    auto reqData = ggapi::Struct::create();
    auto respData =
        ggapi::Task::sendToTopic(ggapi::Symbol{keys.requestDeviceProvisionTopic}, reqData);
    _thingInfo.thingName = respData.get<std::string>("thingName");
    _thingInfo.keyPath = respData.get<std::string>("keyPath");
    _thingInfo.certPath = respData.get<std::string>("certPath");
}
