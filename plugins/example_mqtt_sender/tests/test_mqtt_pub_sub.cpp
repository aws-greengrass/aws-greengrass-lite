#include "test_util.hpp"
#include <catch2/catch_all.hpp>
#include <catch2/trompeloeil.hpp>
#include <temp_module.hpp>

using Catch::Matchers::Equals;
namespace mock = trompeloeil;

class MockListener : PubSubCallback {
public:
    MAKE_MOCK2(publishHandler, ggapi::ObjHandle(ggapi::Symbol, ggapi::Container), override);
    MAKE_MOCK2(subscribeHandler, ggapi::ObjHandle(ggapi::Symbol, ggapi::Container), override);
};

inline auto pubStructMatcher(const ggapi::Container &expectedBase) {
    return mock::make_matcher<ggapi::Container>(
        [](const ggapi::Container &requestBase, const ggapi::Container &expectedBase) {
            ggapi::Struct request{requestBase};
            ggapi::Struct expected{expectedBase};
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
        [](std::ostream &os, const ggapi::Container &expected) {
            // TODO: Add toString method to ggapi::Struct
            os << " Not matching\n";
        },
        expectedBase);
}

inline auto subStructMatcher(const ggapi::Container &expectedBase) {
    return mock::make_matcher<ggapi::Container>(
        [](const ggapi::Container &requestBase, const ggapi::Container &expectedBase) {
            ggapi::Struct request{requestBase};
            ggapi::Struct expected{expectedBase};
            if(!(request.hasKey(keys.topicName) && request.hasKey(keys.qos))) {
                return false;
            }
            return expected.get<std::string>(keys.topicName)
                       == request.get<std::string>(keys.topicName)
                   && expected.get<int>(keys.qos) == request.get<int>(keys.qos);
        },
        [](std::ostream &os, const ggapi::Container &expectedBase) {
            // TODO: Add toString method to ggapi::Struct
            os << " Not matching\n";
        },
        expectedBase);
}

SCENARIO("Example Mqtt Sender pub/sub", "[pubsub]") {
    GIVEN("A sender plugin instance") {
        util::TempModule tempModule{"plugin"};
        TestMqttSender sender = TestMqttSender(*tempModule);
        AND_GIVEN("A mock plugin instance listener") {
            MockListener mockListener;
            util::TempModule testScope{"test"};
            WHEN("The listener subscribes to sender's topic") {
                auto subs = ggapi::Subscription::subscribeToTopic(
                    keys.publishToIoTCoreTopic,
                    ggapi::TopicCallback::of(&MockListener::publishHandler, &mockListener));

                auto expected = ggapi::Struct::create();
                expected.put(keys.topicName, "hello");
                expected.put(keys.qos, 1);
                expected.put(keys.payload, "Hello world!");
                THEN("The listener's publish handler is called") {
                    auto retValue = ggapi::Struct::create().put("status", true);
                    REQUIRE_CALL(
                        mockListener, publishHandler(mock::_, pubStructMatcher(expected)))
                        .RETURN(retValue)
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

                auto subs1 = ggapi::Subscription::subscribeToTopic(
                    keys.publishToIoTCoreTopic,
                    ggapi::TopicCallback::of(&MockListener::publishHandler, &mockListener));
                auto subs2 = ggapi::Subscription::subscribeToTopic(
                    keys.subscribeToIoTCoreTopic,
                    ggapi::TopicCallback::of(&MockListener::subscribeHandler, &mockListener));

                THEN("The listener's publish and subscribe handlers are called") {
                    auto retValue1 = ggapi::Struct::create().put("status", true);
                    REQUIRE_CALL(
                        mockListener,
                        publishHandler(mock::_, pubStructMatcher(pubExpected)))
                        .RETURN(retValue1)
                        .TIMES(AT_LEAST(1));

                    auto retValue2 = ggapi::Struct::create().put(keys.channel, ggapi::Channel::create());
                    REQUIRE_CALL(
                        mockListener,
                        subscribeHandler(mock::_, subStructMatcher(subExpected)))
                        .SIDE_EFFECT(auto message = ggapi::Struct::create();
                                     message.put(keys.topicName, topicName1);
                                     message.put(keys.payload, payload1);
                                     ggapi::Subscription::callTopicAll(keys.mqttPing, message).waitAll();)
                        .RETURN(retValue2)
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
