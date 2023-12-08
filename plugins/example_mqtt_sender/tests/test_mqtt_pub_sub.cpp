#include "test_util.hpp"
#include <catch2/catch_all.hpp>

using Catch::Matchers::Equals;

using namespace std::chrono_literals;

TEST_CASE("Example Mqtt Sender pub/sub", "mqtt") {
    auto pluginScope = ggapi::ModuleScope::registerGlobalPlugin(
        "test", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
    TestExampleMqttSender sender = TestExampleMqttSender(pluginScope);
    SECTION("Publish to Topic") {
        auto testScope = ggapi::ModuleScope::registerGlobalPlugin(
            "test", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
        // create a subscriber
        auto publishHandler = [](ggapi::Task, ggapi::Symbol, ggapi::Struct request) {
            REQUIRE(request.hasKey(keys.topicName));
            REQUIRE(request.hasKey(keys.qos));
            REQUIRE(request.hasKey(keys.payload));

            REQUIRE_THAT(request.get<std::string>(keys.topicName), Equals("hello"));
            REQUIRE(request.get<int>(keys.qos) == 1);
            REQUIRE_THAT(request.get<std::string>(keys.payload), Equals("Hello world!"));

            auto response = ggapi::Struct::create();
            response.put("status", true);
            return response;
        };
        CHECK(testScope.subscribeToTopic(keys.publishToIoTCoreTopic, publishHandler));

        // start the lifecycle
        CHECK(sender.startLifecycle());

        // ensure that you get at least one message
        std::this_thread::sleep_for(2s);

        // check the published message
        auto pubMsg = sender.getPublishMessage();
        REQUIRE(pubMsg.hasKey("status"));
        REQUIRE(pubMsg.get<bool>("status"));

        // stop lifecycle
        CHECK(sender.stopLifecycle());
    }

    SECTION("Subscribe to Topic") {
        // create a publisher
        auto testScope1 = ggapi::ModuleScope::registerGlobalPlugin(
            "test", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
        auto subscribeHandler1 = [](ggapi::Task, ggapi::Symbol, ggapi::Struct request) {
            REQUIRE(request.hasKey(keys.topicFilter));
            REQUIRE(request.hasKey(keys.qos));
            REQUIRE(request.hasKey(keys.lpcResponseTopic));

            auto topicFilter = request.get<std::string>(keys.topicFilter);
            auto lpcResponseTopic = request.get<std::string>(keys.lpcResponseTopic);
            auto qos = request.get<int>(keys.qos);
            REQUIRE_THAT(topicFilter, Equals("ping/#"));
            REQUIRE(qos == 1);
            REQUIRE_THAT(lpcResponseTopic, Equals("mqttPing"));

            // fictitious publish message
            auto message = ggapi::Struct::create();
            message.put(keys.topicName, "ping/hello");
            message.put(keys.payload, "Hello World!");
            auto statusResponse = ggapi::Task::sendToTopic(lpcResponseTopic, message);
            REQUIRE(statusResponse.hasKey("status"));
            REQUIRE(statusResponse.get<bool>("status"));
            return ggapi::Struct::create();
        };

        SECTION("Single Subscription") {
            CHECK(testScope1.subscribeToTopic(keys.subscribeToIoTCoreTopic, subscribeHandler1));

            // start the lifecycle
            CHECK(sender.startLifecycle());

            // check the subscribed message
            auto subMsg = sender.getSubscribeMessage();
            REQUIRE(subMsg.hasKey(keys.topicName));
            REQUIRE(subMsg.hasKey(keys.payload));

            REQUIRE_THAT(subMsg.get<std::string>(keys.topicName), Equals("ping/hello"));
            REQUIRE_THAT(subMsg.get<std::string>(keys.payload), Equals("Hello World!"));

            // stop lifecycle
            CHECK(sender.stopLifecycle());
        }

        SECTION("Multiple Subscriptions") {
            auto testScope2 = ggapi::ModuleScope::registerGlobalPlugin(
                "test2", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });

            // create another publisher
            auto subscribeHandler2 = [](ggapi::Task, ggapi::Symbol, ggapi::Struct request) {
                REQUIRE(request.hasKey(keys.topicFilter));
                REQUIRE(request.hasKey(keys.qos));
                REQUIRE(request.hasKey(keys.lpcResponseTopic));

                auto topicFilter = request.get<std::string>(keys.topicFilter);
                auto lpcResponseTopic = request.get<std::string>(keys.lpcResponseTopic);
                auto qos = request.get<int>(keys.qos);
                REQUIRE_THAT(topicFilter, Equals("ping/#"));
                REQUIRE(qos == 1);
                REQUIRE_THAT(lpcResponseTopic, Equals("mqttPing"));

                // fictitious publish message
                auto message = ggapi::Struct::create();
                message.put(keys.topicName, "ping/clock");
                message.put(keys.payload, "Tick tick!");
                auto statusResponse = ggapi::Task::sendToTopic(lpcResponseTopic, message);
                REQUIRE(statusResponse.hasKey("status"));
                REQUIRE(statusResponse.get<bool>("status"));
                return ggapi::Struct::create();
            };

            CHECK(testScope1.subscribeToTopic(keys.subscribeToIoTCoreTopic, subscribeHandler1));

            // start the lifecycle
            CHECK(sender.startLifecycle());

            // check the subscribed message
            auto subMsg1 = sender.getSubscribeMessage();
            REQUIRE(subMsg1.hasKey(keys.topicName));
            REQUIRE(subMsg1.hasKey(keys.payload));

            REQUIRE_THAT(subMsg1.get<std::string>(keys.topicName), Equals("ping/hello"));
            REQUIRE_THAT(subMsg1.get<std::string>(keys.payload), Equals("Hello World!"));

            CHECK(testScope2.subscribeToTopic(keys.subscribeToIoTCoreTopic, subscribeHandler2));

            auto subMsg2 = sender.getSubscribeMessage();
            REQUIRE(subMsg2.hasKey(keys.topicName));
            REQUIRE(subMsg2.hasKey(keys.payload));

            REQUIRE_THAT(subMsg2.get<std::string>(keys.topicName), Equals("ping/clock"));
            REQUIRE_THAT(subMsg2.get<std::string>(keys.payload), Equals("Tick tick!"));

            // stop lifecycle
            CHECK(sender.stopLifecycle());
        }
    }
}
