#include "authorization_handler.hpp"
#include <catch2/catch_all.hpp>
#include <test/plugin_lifecycle.hpp>

// NOLINTBEGIN

using namespace authorization;
using namespace test;

namespace authorization_handler_tests {
    void sampleMoreInit(Lifecycle &data) {
        const auto servicesStr = R"(
properAllService:
  dependencies: []
  version: "0.0.0"
  configuration:
    accessControl:
      aws.greengrass.ipc.pubsub:
        "properAllService:pubsub:1":
          policyDescription: Allows access to publish to all topics.
          operations:
            - "aws.greengrass#PublishToTopic"
          resources:
            - "*"
properWildMQTTService:
  dependencies: []
  version: "0.0.0"
  configuration:
    accessControl:
      aws.greengrass.ipc.mqttproxy:
        "properWildMQTTService:mqttproxy:1":
          policyDescription: Allows access to publish to wild mqtt topics.
          operations:
            - "aws.greengrass#SubscribeToIoTCore"
          resources:
            - "topic/*/get/*"
properExactService:
  dependencies: []
  version: "0.0.0"
  configuration:
    accessControl:
      aws.greengrass.ipc.mqttproxy:
        "properExactService:mqttproxy:1":
          policyDescription: Allows access to publish to an exact topic.
          operations:
            - "aws.greengrass#PublishToIoTCore"
          resources:
            - "exact"
improperService:
  dependencies: []
  version: "0.0.0"
  configuration:
    accessControl:
      aws.greengrass.ipc.pubsub:
        "improperService:pubsub:1":
          policyDescription: Allows access to publish to all topics.
          operations: "aws.greengrass#PublishToTopic"
        resources:
          - "anExactResource"
)";

        ggapi::Buffer buffer = ggapi::Buffer::create();
        buffer.put(0, std::string_view(servicesStr));
        ggapi::Container c = buffer.fromYaml();
        ggapi::Struct serviceAsStruct(c);

        auto properAllService =
            serviceAsStruct.get<ggapi::Struct>(serviceAsStruct.foldKey("properAllService"));
        auto properExactService =
            serviceAsStruct.get<ggapi::Struct>(serviceAsStruct.foldKey("properExactService"));
        auto properWildMQTTService =
            serviceAsStruct.get<ggapi::Struct>(serviceAsStruct.foldKey("properWildMQTTService"));
        auto improperService =
            serviceAsStruct.get<ggapi::Struct>(serviceAsStruct.foldKey("improperService"));
        data._services.put("properAllService", properAllService);
        data._services.put("properWildMQTTService", properWildMQTTService);
        data._services.put("properExactService", properExactService);
        data._services.put("improperService", improperService);
    }

    SCENARIO("Authorization Handler", "[authorization_handler]") {

        GIVEN("The plugin with the sample services in config struct") {
            AuthorizationHandler plugin{};
            Lifecycle lifecycle{"aws.greengrass.authorization_handler", plugin, sampleMoreInit};
            WHEN("The plugin starts its lifecycle step") {
                lifecycle.start();
                THEN("Make call 1 on the plugin to check if authorized") {
                    auto request{ggapi::Struct::create()};
                    request.put("destination", "aws.greengrass.ipc.pubsub");
                    request.put("principal", "properAllService");
                    request.put("operation", "aws.greengrass#PublishToTopic");
                    request.put("resource", "any");
                    request.put("resourceType", "");

                    auto future = ggapi::Subscription::callTopicFirst(
                        ggapi::Symbol{"aws.greengrass.checkAuthorized"}, request);
                    REQUIRE(future);
                    REQUIRE_NOTHROW(future.waitAndGetValue());
                }
                AND_THEN("Make call 2 on the plugin to check if authorized") {
                    auto request{ggapi::Struct::create()};
                    request.put("destination", "aws.greengrass.ipc.mqttproxy");
                    request.put("principal", "properExactService");
                    request.put("operation", "aws.greengrass#PublishToIoTCore");
                    request.put("resource", "exact");
                    request.put("resourceType", "MQTT");

                    auto future = ggapi::Subscription::callTopicFirst(
                        ggapi::Symbol{"aws.greengrass.checkAuthorized"}, request);
                    REQUIRE(future);
                    REQUIRE_NOTHROW(future.waitAndGetValue());
                }
                AND_THEN("Make call 3 on the plugin to check if authorized") {
                    auto request{ggapi::Struct::create()};
                    request.put("destination", "aws.greengrass.ipc.mqttproxy");
                    request.put("principal", "properExactService");
                    request.put("operation", "aws.greengrass#PublishToIoTCore");
                    request.put("resource", "notexact");
                    request.put("resourceType", "MQTT");

                    auto future = ggapi::Subscription::callTopicFirst(
                        ggapi::Symbol{"aws.greengrass.checkAuthorized"}, request);
                    REQUIRE(future);
                    REQUIRE_THROWS(future.waitAndGetValue());
                }
                AND_THEN("Make call 4 on the plugin to check if authorized") {
                    auto request{ggapi::Struct::create()};
                    request.put("destination", "aws.greengrass.ipc.pubsub");
                    request.put("principal", "improperService");
                    request.put("operation", "aws.greengrass#PublishToTopic");
                    request.put("resource", "anything");
                    request.put("resourceType", "");

                    auto future = ggapi::Subscription::callTopicFirst(
                        ggapi::Symbol{"aws.greengrass.checkAuthorized"}, request);
                    REQUIRE(future);
                    REQUIRE_THROWS(future.waitAndGetValue());
                }
                AND_THEN("Make call 5 on the plugin to check if authorized") {
                    auto request{ggapi::Struct::create()};
                    request.put("destination", "aws.greengrass.ipc.mqttproxy");
                    request.put("principal", "properWildMQTTService");
                    request.put("operation", "aws.greengrass#SubscribeToIoTCore");
                    request.put("resource", "topic/+/get/#");
                    request.put("resourceType", "MQTT");

                    auto future = ggapi::Subscription::callTopicFirst(
                        ggapi::Symbol{"aws.greengrass.checkAuthorized"}, request);
                    REQUIRE(future);
                    REQUIRE_NOTHROW(future.waitAndGetValue());
                }
            }
        }
    }
} // namespace authorization_handler_tests

// NOLINTEND
