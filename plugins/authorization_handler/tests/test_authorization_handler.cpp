#include "authorization_handler.hpp"
#include <catch2/catch_all.hpp>
#include <test/plugin_lifecycle.hpp>

// NOLINTBEGIN

using namespace authorization;
using namespace test;

namespace authorization_handler_tests {
    void sampleMoreInit(Lifecycle &data) {
        const auto serviceStr = R"(
testService:
  dependencies: []
  version: "0.0.0"
  configuration:
    accessControl:
      aws.greengrass.ipc.pubsub:
        "testService:pubsub:1":
          policyDescription: Allows access to publish to all topics.
          operations:
            - "aws.greengrass#PublishToTopic"
          resources:
            - "*"
testService2:
  dependencies: []
  version: "0.0.0"
  configuration:
    accessControl:
      aws.greengrass.ipc.pubsub:
        "testService2:pubsub:1":
          policyDescription: Allows access to publish to all topics.
          operations:
            - "aws.greengrass#PublishToTopic"
          resources:
            - "anExactResource"
)";

        ggapi::Buffer buffer = ggapi::Buffer::create();
        buffer.put(0, std::string_view(serviceStr));
        ggapi::Container c = buffer.fromYaml();
        ggapi::Struct serviceAsStruct(c);

        auto testService =
            serviceAsStruct.get<ggapi::Struct>(serviceAsStruct.foldKey("testService"));
        auto testService2 =
            serviceAsStruct.get<ggapi::Struct>(serviceAsStruct.foldKey("testService2"));
        data._services.put("testService", testService);
        data._services.put("testService2", testService2);
    }

    SCENARIO("Authorization Handler", "[authorization_handler]") {
        GIVEN("Initiate the plugin") {
            // start the lifecycle
            AuthorizationHandler plugin{};
            Lifecycle lifecycle{"aws.greengrass.authorization_handler", plugin, sampleMoreInit};
            lifecycle.start();
            REQUIRE(true);
        }
    }
} // namespace authorization_handler_tests

// NOLINTEND
