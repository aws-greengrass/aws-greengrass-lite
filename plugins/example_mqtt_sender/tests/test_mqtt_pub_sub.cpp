#include "test_util.hpp"
#include <catch2/catch_all.hpp>
#include <catch2/trompeloeil.hpp>

using Catch::Matchers::Equals;
namespace mock = trompeloeil;

class MockListener : PubSubCallback {
public:
    MAKE_MOCK3(publishHandler, ggapi::Struct(ggapi::Task, ggapi::Symbol, ggapi::Struct), override);
    MAKE_MOCK3(
        subscribeHandler, ggapi::Struct(ggapi::Task, ggapi::Symbol, ggapi::Struct), override);
};

inline auto pubStructMatcher(ggapi::Struct expected) {
    return mock::make_matcher<ggapi::Struct>(
        [](const ggapi::Struct &request, const ggapi::Struct &expected) {
            if(!(request.hasKey(keys.topicName) && request.hasKey(keys.qos)
                 && request.hasKey(keys.payload))) {
                return false;
            }
            return expected.get<std::string>(keys.topicName)
                       == request.get<std::string>(keys.topicName)
                   && expected.get<int>(keys.qos) == request.get<int>(keys.qos)
                   && expected.get<std::string>(keys.payload)
                          == request.get<std::string>(keys.payload);
        },
        [](std::ostream &os, const ggapi::Struct &expected) {
            // TODO: Add toString method to ggapi::Struct
            os << " Not matching\n";
        },
        expected);
}

inline auto subStructMatcher(ggapi::Struct expected) {
    return mock::make_matcher<ggapi::Struct>(
        [](const ggapi::Struct &request, const ggapi::Struct &expected) {
            if(!(request.hasKey(keys.topicName) && request.hasKey(keys.qos))) {
                return false;
            }
            return expected.get<std::string>(keys.topicName)
                       == request.get<std::string>(keys.topicName)
                   && expected.get<int>(keys.qos) == request.get<int>(keys.qos);
        },
        [](std::ostream &os, const ggapi::Struct &expected) {
            // TODO: Add toString method to ggapi::Struct
            os << " Not matching\n";
        },
        expected);
}

SCENARIO("Example Mqtt Sender pub/sub", "[pubsub]") {
    GIVEN("A sender plugin instance") {
        auto moduleScope = ggapi::ModuleScope::registerGlobalPlugin(
            "plugin", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
        TestMqttSender sender = TestMqttSender(moduleScope);
        moduleScope.setActive();
        AND_GIVEN("A mock plugin instance listener") {
            MockListener mockListener;
            auto testScope = ggapi::ModuleScope::registerGlobalPlugin(
                "test", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
            testScope.setActive();
            WHEN("The listener subscribes to sender's topic") {
                std::ignore = testScope.subscribeToTopic(
                    keys.publishToIoTCoreTopic,
                    ggapi::TopicCallback::of(&MockListener::publishHandler, mockListener));

                auto expected = ggapi::Struct::create();
                expected.put(keys.topicName, "hello");
                expected.put(keys.qos, 1);
                expected.put(keys.payload, "Hello world!");
                THEN("The listener's publish handler is called") {
                    REQUIRE_CALL(
                        mockListener, publishHandler(mock::_, mock::_, pubStructMatcher(expected)))
                        .RETURN(ggapi::Struct::create().put("status", true))
                        .TIMES(AT_LEAST(1));

                    // start the lifecycle
                    CHECK(sender.startLifecycle());

                    // wait for the lifecycle to start
                    sender.wait();
                }
            }

            WHEN("The listener both subscribe and publish to sender's respective topics") {
                auto subExpected = ggapi::Struct::create();
                subExpected.put(keys.topicName, "ping/#");
                subExpected.put(keys.qos, 1);

                auto pubExpected = ggapi::Struct::create();
                pubExpected.put(keys.topicName, "hello");
                pubExpected.put(keys.qos, 1);
                pubExpected.put(keys.payload, "Hello world!");

                // response values
                auto topicName1 = "ping/hello";
                auto payload1 = "Hello World!";

                std::ignore = testScope.subscribeToTopic(
                    keys.publishToIoTCoreTopic,
                    ggapi::TopicCallback::of(&MockListener::publishHandler, mockListener));
                std::ignore = testScope.subscribeToTopic(
                    keys.subscribeToIoTCoreTopic,
                    ggapi::TopicCallback::of(&MockListener::subscribeHandler, mockListener));

                THEN("The listener's publish and subscribe handlers are called") {
                    REQUIRE_CALL(
                        mockListener,
                        publishHandler(mock::_, mock::_, pubStructMatcher(pubExpected)))
                        .RETURN(ggapi::Struct::create().put("status", true))
                        .TIMES(AT_LEAST(1));

                    REQUIRE_CALL(
                        mockListener,
                        subscribeHandler(mock::_, mock::_, subStructMatcher(subExpected)))
                        .SIDE_EFFECT(auto message = ggapi::Struct::create();
                                     message.put(keys.topicName, topicName1);
                                     message.put(keys.payload, payload1);
                                     std::ignore =
                                         ggapi::Task::sendToTopic(keys.mqttPing, message);)
                        .RETURN(ggapi::Struct::create().put(keys.channel, ggapi::Channel::create()))
                        .TIMES(AT_LEAST(1));

                    // start the lifecycle
                    CHECK(sender.startLifecycle());

                    // wait for the lifecycle to start
                    sender.wait();
                }
            }

            // stop lifecycle
            CHECK(sender.stopLifecycle());
        }
    }
}
