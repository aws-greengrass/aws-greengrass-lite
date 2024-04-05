#include <cpp_api.hpp>
#include <ctime>
#include <plugin.hpp>
#include <string>

const static auto LOG = ggapi::Logger::of("FleetStatusService");

struct Keys {
    ggapi::StringOrd topicName{"topicName"};
    ggapi::Symbol qos{"qos"};
    ggapi::Symbol payload{"payload"};
    ggapi::Symbol errorCode{"errorCode"};
    ggapi::Symbol publishToIoTCoreTopic{"aws.greengrass.PublishToIoTCore"};
};

static const Keys keys;

class FleetStatusService : public ggapi::Plugin {
private:
public:
    bool onStart(ggapi::Struct data) override;

    static FleetStatusService &get() {
        static FleetStatusService instance{};
        return instance;
    }
};

bool FleetStatusService::onStart(ggapi::Struct data) {
    auto thingName = data.getValue<std::string>({"system", "thingName"});

    std::string json;
    auto buf = ggapi::Struct::create()
                   .put(
                       // TODO: Fill in real values
                       {{"ggcVersion", "2.13.0"},
                        {"platform", "linux"},
                        {"architecture", "amd64"},
                        {"thing", thingName},
                        {"sequenceNumber", 1},
                        {"timestamp", std::time(nullptr)},
                        {"messageType", "COMPLETE"},
                        {"trigger", "NUCLEUS_LAUNCH"},
                        {"overallDeviceStatus", "HEALTHY"},
                        {"components", ggapi::List::create()}})
                   .toJson();
    json.resize(buf.size());
    buf.get(0, json);

    auto value = ggapi::Struct::create().put(
        {{keys.topicName, "$aws/things/" + thingName + "/greengrassv2/health/json"},
         {keys.qos, 1},
         {keys.payload, json}});

    LOG.atInfo("MQTT-startup-notify")
        .kv(keys.payload, json)
        .log("Sending Fleet Status Service update.");

    auto responseFuture = ggapi::Subscription::callTopicFirst(keys.publishToIoTCoreTopic, value);
    if(!responseFuture) {
        LOG.atError("MQTT-message-call-failed").log("Failed to send MQTT message.");
    } else {
        responseFuture.whenValid([](const ggapi::Future &completedFuture) {
            try {
                auto response = ggapi::Struct(completedFuture.getValue());
                if(response.get<int>(keys.errorCode) == 0) {
                    LOG.atInfo("MQTT-message-send-success")
                        .log("Successfully sent Fleet Status Service update.");
                } else {
                    LOG.atError("MQTT-message-send-error")
                        .log("Failed to send Fleet Status Service update.");
                }
            } catch(const ggapi::GgApiError &error) {
                LOG.atError("MQTT-message-send-throw")
                    .cause(error)
                    .log("Failed to send Fleet Status Service update.");
            }
        });
    }

    return true;
}

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data, bool *pHandled) noexcept {
    return FleetStatusService::get().lifecycle(moduleHandle, phase, data, pHandled);
}
