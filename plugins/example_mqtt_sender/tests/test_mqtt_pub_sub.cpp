#include "test_util.hpp"
#include <catch2/catch_all.hpp>
#include <catch2/trompeloeil.hpp>

using namespace std::chrono_literals;

using Catch::Matchers::Equals;
using trompeloeil::_;

class MockPubSubCallback : PubSubCallback {
public:
    MAKE_MOCK3(publishHandler, ggapi::Struct(ggapi::Task, ggapi::Symbol, ggapi::Struct), override);
    MAKE_MOCK3(
        subscribeHandler, ggapi::Struct(ggapi::Task, ggapi::Symbol, ggapi::Struct), override);
};

inline auto pubStructMatcher(ggapi::Struct expected) {
    return trompeloeil::make_matcher<ggapi::Struct>(
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
    return trompeloeil::make_matcher<ggapi::Struct>(
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

TEST_CASE("Example Mqtt Sender publish", "[pubsub]") {
    auto pluginScope = ggapi::ModuleScope::registerGlobalPlugin(
        "test", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
    TestExampleMqttSender sender = TestExampleMqttSender(pluginScope);
    // create a subscriber
    MockPubSubCallback mockCallback1;
    auto testScope1 = ggapi::ModuleScope::registerGlobalPlugin(
        "test1", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });

    CHECK(testScope1.subscribeToTopic(
        keys.publishToIoTCoreTopic,
        ggapi::TopicCallback::of(&MockPubSubCallback::publishHandler, mockCallback1)));

    auto expected = ggapi::Struct::create();
    expected.put(keys.topicName, "hello");
    expected.put(keys.qos, 1);
    expected.put(keys.payload, "Hello world!");

    SECTION("Single subscriber") {
        REQUIRE_CALL(mockCallback1, publishHandler(_, _, pubStructMatcher(expected)))
            .RETURN(ggapi::Struct::create().put("status", true))
            .TIMES(AT_LEAST(1));

        // start the lifecycle
        CHECK(sender.startLifecycle());

        // wait for the lifecycle to start
        std::this_thread::sleep_for(1s);

        // verify the publish response message
        auto pubMsg = sender.getPublishMessage();
        REQUIRE(pubMsg.hasKey("status"));
        REQUIRE(pubMsg.get<bool>("status"));
    }

    SECTION("Multiple subscribers") {
        // create another subscriber
        MockPubSubCallback mockCallback2;
        auto testScope2 = ggapi::ModuleScope::registerGlobalPlugin(
            "test2", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });

        CHECK(testScope2.subscribeToTopic(
            keys.publishToIoTCoreTopic,
            ggapi::TopicCallback::of(&MockPubSubCallback::publishHandler, mockCallback2)));

        REQUIRE_CALL(mockCallback1, publishHandler(_, _, pubStructMatcher(expected)))
            .RETURN(ggapi::Struct::create().put("status", true))
            .TIMES(AT_LEAST(1));

        REQUIRE_CALL(mockCallback2, publishHandler(_, _, pubStructMatcher(expected)))
            .RETURN(ggapi::Struct::create().put("status", true))
            .TIMES(AT_LEAST(1));

        // start the lifecycle
        CHECK(sender.startLifecycle());

        // wait for the lifecycle to start
        std::this_thread::sleep_for(1s);
    }
    // stop lifecycle
    CHECK(sender.stopLifecycle());
}

TEST_CASE("Example Mqtt Sender subscribe", "[pubsub]") {
    auto pluginScope = ggapi::ModuleScope::registerGlobalPlugin(
        "test", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
    TestExampleMqttSender sender = TestExampleMqttSender(pluginScope);
    // create a publisher
    MockPubSubCallback mockCallback1;
    auto testScope1 = ggapi::ModuleScope::registerGlobalPlugin(
        "test", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });

    auto expected = ggapi::Struct::create();
    expected.put(keys.topicFilter, "ping/#");
    expected.put(keys.qos, 1);
    expected.put(keys.lpcResponseTopic, "mqttPing");

    // response values
    auto topicName1 = "ping/hello";
    auto payload1 = "Hello World!";

    CHECK(testScope1.subscribeToTopic(
        keys.subscribeToIoTCoreTopic,
        ggapi::TopicCallback::of(&MockPubSubCallback::subscribeHandler, mockCallback1)));

    SECTION("Single Publisher") {
        REQUIRE_CALL(mockCallback1, subscribeHandler(_, _, subStructMatcher(expected)))
            .SIDE_EFFECT(auto message = ggapi::Struct::create();
                         message.put(keys.topicName, topicName1);
                         message.put(keys.payload, payload1);
                         std::ignore = ggapi::Task::sendToTopic(keys.mqttPing, message);)
            .RETURN(ggapi::Struct::create().put("status", true))
            .TIMES(AT_LEAST(1));

        // start the lifecycle
        CHECK(sender.startLifecycle());

        // check the subscribed message
        auto subMsg = sender.getSubscribeMessage();
        REQUIRE(subMsg.hasKey(keys.topicName));
        REQUIRE(subMsg.hasKey(keys.payload));

        REQUIRE_THAT(subMsg.get<std::string>(keys.topicName), Equals(topicName1));
        REQUIRE_THAT(subMsg.get<std::string>(keys.payload), Equals(payload1));
    }

    SECTION("Multiple publishers") {
        // create another publisher
        MockPubSubCallback mockCallback2;
        auto testScope2 = ggapi::ModuleScope::registerGlobalPlugin(
            "test2", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });

        CHECK(testScope2.subscribeToTopic(
            keys.subscribeToIoTCoreTopic,
            ggapi::TopicCallback::of(&MockPubSubCallback::subscribeHandler, mockCallback2)));

        SECTION("Multiple publishers to same topic") {
            auto topicName2 = topicName1;
            auto payload2 = "Sweet home!";

            REQUIRE_CALL(mockCallback1, subscribeHandler(_, _, subStructMatcher(expected)))
                .SIDE_EFFECT(auto message = ggapi::Struct::create();
                             message.put(keys.topicName, topicName1);
                             message.put(keys.payload, payload1);
                             std::ignore = ggapi::Task::sendToTopic(keys.mqttPing, message);)
                .RETURN(ggapi::Struct::create().put("status", true))
                .TIMES(AT_LEAST(1));

            REQUIRE_CALL(mockCallback2, subscribeHandler(_, _, subStructMatcher(expected)))
                .SIDE_EFFECT(auto message = ggapi::Struct::create();
                             message.put(keys.topicName, topicName2);
                             message.put(keys.payload, payload2);
                             std::ignore = ggapi::Task::sendToTopic(keys.mqttPing, message);)
                .RETURN(ggapi::Struct::create().put("status", true))
                .TIMES(AT_LEAST(1));

            // start the lifecycle
            CHECK(sender.startLifecycle());
        }

        SECTION("Multiple publishers to different topics") {
            auto topicName2 = "ping/clock";
            auto payload2 = "Sweet home!";

            REQUIRE_CALL(mockCallback1, subscribeHandler(_, _, subStructMatcher(expected)))
                .SIDE_EFFECT(auto message = ggapi::Struct::create();
                             message.put(keys.topicName, topicName1);
                             message.put(keys.payload, payload1);
                             std::ignore = ggapi::Task::sendToTopic(keys.mqttPing, message);)
                .RETURN(ggapi::Struct::create().put("status", true))
                .TIMES(AT_LEAST(1));

            REQUIRE_CALL(mockCallback2, subscribeHandler(_, _, subStructMatcher(expected)))
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
    // stop lifecycle
    CHECK(sender.stopLifecycle());
}

TEST_CASE("Example Mqtt Sender publish and subscribe", "[pubsub]") {
    auto pluginScope = ggapi::ModuleScope::registerGlobalPlugin(
        "test", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
    TestExampleMqttSender sender = TestExampleMqttSender(pluginScope);
    // create a publisher
    MockPubSubCallback mockCallback;
    auto testScope = ggapi::ModuleScope::registerGlobalPlugin(
        "test", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });

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

    CHECK(testScope.subscribeToTopic(
        keys.publishToIoTCoreTopic,
        ggapi::TopicCallback::of(&MockPubSubCallback::publishHandler, mockCallback)));
    CHECK(testScope.subscribeToTopic(
        keys.subscribeToIoTCoreTopic,
        ggapi::TopicCallback::of(&MockPubSubCallback::subscribeHandler, mockCallback)));

    SECTION("Single publisher and subscriber") {
        REQUIRE_CALL(mockCallback, publishHandler(_, _, pubStructMatcher(pubExpected)))
            .RETURN(ggapi::Struct::create().put("status", true))
            .TIMES(AT_LEAST(1));

        REQUIRE_CALL(mockCallback, subscribeHandler(_, _, subStructMatcher(subExpected)))
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

        // verify the publish response message
        auto pubMsg = sender.getPublishMessage();
        REQUIRE(pubMsg.hasKey("status"));
        REQUIRE(pubMsg.get<bool>("status"));

        // check the subscribed message
        auto subMsg = sender.getSubscribeMessage();
        REQUIRE(subMsg.hasKey(keys.topicName));
        REQUIRE(subMsg.hasKey(keys.payload));

        REQUIRE_THAT(subMsg.get<std::string>(keys.topicName), Equals(topicName1));
        REQUIRE_THAT(subMsg.get<std::string>(keys.payload), Equals(payload1));
    }
    // stop lifecycle
    CHECK(sender.stopLifecycle());
}
