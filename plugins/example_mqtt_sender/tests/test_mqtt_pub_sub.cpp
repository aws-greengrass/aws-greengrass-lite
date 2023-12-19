#include "test_util.hpp"
#include <catch2/catch_all.hpp>
#include <catch2/trompeloeil.hpp>

using Catch::Matchers::Equals;
namespace mock = trompeloeil;

class MockPubSubCallback : PubSubCallback {
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
            if(!(request.hasKey(keys.topicFilter) && request.hasKey(keys.qos)
                 && request.hasKey(keys.lpcResponseTopic))) {
                return false;
            }
            return expected.get<std::string>(keys.topicFilter)
                       == request.get<std::string>(keys.topicFilter)
                   && expected.get<int>(keys.qos) == request.get<int>(keys.qos)
                   && expected.get<std::string>(keys.lpcResponseTopic)
                          == request.get<std::string>(keys.lpcResponseTopic);
        },
        [](std::ostream &os, const ggapi::Struct &expected) {
            // TODO: Add toString method to ggapi::Struct
            os << " Not matching\n";
        },
        expected);
}

SCENARIO("Example Mqtt Sender pub/sub", "[pubsub]") {
    GIVEN("A sender plugin instance") {
        auto pluginScope = ggapi::ModuleScope::registerGlobalPlugin(
            "plugin", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
        pluginScope.setActive();
        TestExampleMqttSender sender = TestExampleMqttSender(pluginScope);
        AND_GIVEN("A mock receiver plugin's callbacks") {
            MockPubSubCallback mockCallback1;
            auto testScope1 = ggapi::ModuleScope::registerGlobalPlugin(
                "test", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });

            WHEN("The receiver subscribes to sender's topic") {
                CHECK(testScope1.subscribeToTopic(
                    keys.publishToIoTCoreTopic,
                    ggapi::TopicCallback::of(&MockPubSubCallback::publishHandler, mockCallback1)));

                auto expected = ggapi::Struct::create();
                expected.put(keys.topicName, "hello");
                expected.put(keys.qos, 1);
                expected.put(keys.payload, "Hello world!");
                THEN("The receiver publish handler is called") {
                    REQUIRE_CALL(mockCallback1, publishHandler(mock::_, mock::_, pubStructMatcher(expected)))
                        .RETURN(ggapi::Struct::create().put("status", true))
                        .TIMES(AT_LEAST(1));

                    // start the lifecycle
                    CHECK(sender.startLifecycle());

                    // wait for the lifecycle to start
                    std::this_thread::sleep_for(1s);
                }

                AND_WHEN("Another receiver subscribes to same topic") {
                    // create another subscriber
                    MockPubSubCallback mockCallback2;
                    auto testScope2 = ggapi::ModuleScope::registerGlobalPlugin(
                        "test2", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
                    CHECK(testScope2.subscribeToTopic(
                        keys.publishToIoTCoreTopic,
                        ggapi::TopicCallback::of(&MockPubSubCallback::publishHandler, mockCallback2)));
                    THEN("Both receivers' publish handlers are called") {
                        REQUIRE_CALL(mockCallback1, publishHandler(mock::_, mock::_, pubStructMatcher(expected)))
                            .RETURN(ggapi::Struct::create().put("status", true))
                            .TIMES(AT_LEAST(1));

                        REQUIRE_CALL(mockCallback2, publishHandler(mock::_, mock::_, pubStructMatcher(expected)))
                            .RETURN(ggapi::Struct::create().put("status", true))
                            .TIMES(AT_LEAST(1));

                        // start the lifecycle
                        CHECK(sender.startLifecycle());

                        // wait for the lifecycle to start
                        std::this_thread::sleep_for(1s);
                    }
                }
            }

            WHEN("The receiver publishes to sender's topic") {
                // create a publisher
                CHECK(testScope1.subscribeToTopic(
                    keys.subscribeToIoTCoreTopic,
                    ggapi::TopicCallback::of(&MockPubSubCallback::subscribeHandler, mockCallback1)));

                auto expected = ggapi::Struct::create();
                expected.put(keys.topicFilter, "ping/#");
                expected.put(keys.qos, 1);
                expected.put(keys.lpcResponseTopic, "mqttPing");

                // response values
                auto topicName1 = "ping/hello";
                auto payload1 = "Hello World!";

                // TODO: Why need this?
                auto pubExpected = ggapi::Struct::create();
                pubExpected.put(keys.topicName, "hello");
                pubExpected.put(keys.qos, 1);
                pubExpected.put(keys.payload, "Hello world!");
                REQUIRE_CALL(mockCallback1, publishHandler(mock::_, mock::_, pubStructMatcher(pubExpected)))
                    .RETURN(ggapi::Struct::create().put("status", true))
                    .TIMES(AT_LEAST(1));

                AND_WHEN("Fix this") {
                    CHECK(testScope1.subscribeToTopic(
                        keys.publishToIoTCoreTopic,
                        ggapi::TopicCallback::of(
                            &MockPubSubCallback::publishHandler, mockCallback1)));
                    THEN("The sender's subscribe callback is called") {
                        REQUIRE_CALL(
                            mockCallback1, subscribeHandler(mock::_, mock::_, subStructMatcher(expected)))
                            .SIDE_EFFECT(auto message = ggapi::Struct::create();
                                         message.put(keys.topicName, topicName1);
                                         message.put(keys.payload, payload1);
                                         std::ignore =
                                             ggapi::Task::sendToTopic(keys.mqttPing, message);)
                            .RETURN(ggapi::Struct::create().put("status", true))
                            .TIMES(AT_LEAST(1));

                        // start the lifecycle
                        CHECK(sender.startLifecycle());
                    }
                }

                AND_WHEN("Another receiver publishes to same topic") {
                    // create another publisher
                    MockPubSubCallback mockCallback2;
                    auto testScope2 = ggapi::ModuleScope::registerGlobalPlugin(
                        "test2", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });

                    CHECK(testScope2.subscribeToTopic(
                        keys.subscribeToIoTCoreTopic,
                        ggapi::TopicCallback::of(&MockPubSubCallback::subscribeHandler, mockCallback2)));

                    auto topicName2 = topicName1;
                    auto payload2 = "Sweet home!";

                    THEN("Both receiver subscribe handlers are called") {
                        REQUIRE_CALL(mockCallback1, subscribeHandler(mock::_, mock::_, subStructMatcher(expected)))
                            .SIDE_EFFECT(auto message = ggapi::Struct::create();
                                         message.put(keys.topicName, topicName1);
                                         message.put(keys.payload, payload1);
                                         std::ignore = ggapi::Task::sendToTopic(keys.mqttPing, message);)
                            .RETURN(ggapi::Struct::create().put("status", true))
                            .TIMES(AT_LEAST(1));

                        REQUIRE_CALL(mockCallback2, subscribeHandler(mock::_, mock::_, subStructMatcher(expected)))
                            .SIDE_EFFECT(auto message = ggapi::Struct::create();
                                         message.put(keys.topicName, topicName2);
                                         message.put(keys.payload, payload2);
                                         std::ignore = ggapi::Task::sendToTopic(keys.mqttPing, message);)
                            .RETURN(ggapi::Struct::create().put("status", true))
                            .TIMES(AT_LEAST(1));

                        // start the lifecycle
                        CHECK(sender.startLifecycle());
                    }
                }

                AND_WHEN("Another receiver publishes to a different topic") {
                    // create another publisher
                    MockPubSubCallback mockCallback2;
                    auto testScope2 = ggapi::ModuleScope::registerGlobalPlugin(
                        "test2", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });

                    CHECK(testScope2.subscribeToTopic(
                        keys.subscribeToIoTCoreTopic,
                        ggapi::TopicCallback::of(&MockPubSubCallback::subscribeHandler, mockCallback2)));

                    auto topicName2 = "ping/clock";
                    auto payload2 = "Sweet home!";

                    THEN("") {
                        REQUIRE_CALL(mockCallback1, subscribeHandler(mock::_, mock::_, subStructMatcher(expected)))
                            .SIDE_EFFECT(auto message = ggapi::Struct::create();
                                         message.put(keys.topicName, topicName1);
                                         message.put(keys.payload, payload1);
                                         std::ignore = ggapi::Task::sendToTopic(keys.mqttPing, message);)
                            .RETURN(ggapi::Struct::create().put("status", true))
                            .TIMES(AT_LEAST(1));

                        REQUIRE_CALL(mockCallback2, subscribeHandler(mock::_, mock::_, subStructMatcher(expected)))
                            .SIDE_EFFECT(auto message = ggapi::Struct::create();
                                         message.put(keys.topicName, topicName2);
                                         message.put(keys.payload, payload2);
                                         std::ignore = ggapi::Task::sendToTopic(keys.mqttPing, message);)
                            .RETURN(ggapi::Struct::create().put("status", true))
                            .TIMES(AT_LEAST(1));

                        // start the lifecycle
                        CHECK(sender.startLifecycle());
                    }
                }
            }

            WHEN("The receiver both subscribe and publish to sender's respective topics") {
                auto subExpected = ggapi::Struct::create();
                subExpected.put(keys.topicFilter, "ping/#");
                subExpected.put(keys.qos, 1);
                subExpected.put(keys.lpcResponseTopic, "mqttPing");

                auto pubExpected = ggapi::Struct::create();
                pubExpected.put(keys.topicName, "hello");
                pubExpected.put(keys.qos, 1);
                pubExpected.put(keys.payload, "Hello world!");

                // response values
                auto topicName1 = "ping/hello";
                auto payload1 = "Hello World!";

                CHECK(testScope1.subscribeToTopic(
                    keys.publishToIoTCoreTopic,
                    ggapi::TopicCallback::of(&MockPubSubCallback::publishHandler, mockCallback1)));
                CHECK(testScope1.subscribeToTopic(
                    keys.subscribeToIoTCoreTopic,
                    ggapi::TopicCallback::of(&MockPubSubCallback::subscribeHandler, mockCallback1)));

                THEN("The receiver's publish and subscribe handlers are called") {
                    REQUIRE_CALL(mockCallback1, publishHandler(mock::_, mock::_, pubStructMatcher(pubExpected)))
                        .RETURN(ggapi::Struct::create().put("status", true))
                        .TIMES(AT_LEAST(1));

                    REQUIRE_CALL(mockCallback1, subscribeHandler(mock::_, mock::_, subStructMatcher(subExpected)))
                        .SIDE_EFFECT(auto message = ggapi::Struct::create();
                                     message.put(keys.topicName, topicName1);
                                     message.put(keys.payload, payload1);
                                     std::ignore = ggapi::Task::sendToTopic(keys.mqttPing, message);)
                        .RETURN(ggapi::Struct::create().put("status", true))
                        .TIMES(AT_LEAST(1));

                    // start the lifecycle
                    CHECK(sender.startLifecycle());

                    // wait for the lifecycle to start
                    std::this_thread::sleep_for(1s);
                }
            }
        }
        // stop lifecycle
        CHECK(sender.stopLifecycle());
    }
}
