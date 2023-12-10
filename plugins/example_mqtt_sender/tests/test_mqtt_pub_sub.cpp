#include "test_util.hpp"
#include <catch2/catch_all.hpp>

using Catch::Matchers::Equals;

using namespace std::chrono_literals;

TEST_CASE("Example Mqtt Sender pub/sub", "mqtt") {
    auto pluginScope = ggapi::ModuleScope::registerGlobalPlugin(
        "test", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
    TestExampleMqttSender sender = TestExampleMqttSender(pluginScope);

    auto topicName = "hello";
    auto qos = 1;
    auto payload = "Hello world!";

    SECTION("Publish to Topic") {
        auto testScope1 = ggapi::ModuleScope::registerGlobalPlugin(
            "test1", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
        // create a subscriber
        auto publishHandler1 = [topicName, qos, payload](ggapi::Task, ggapi::Symbol, ggapi::Struct request) {
            REQUIRE(request.hasKey(keys.topicName));
            REQUIRE(request.hasKey(keys.qos));
            REQUIRE(request.hasKey(keys.payload));

            REQUIRE_THAT(request.get<std::string>(keys.topicName), Equals(topicName));
            REQUIRE(request.get<int>(keys.qos) == 1);
            REQUIRE_THAT(request.get<std::string>(keys.payload), Equals(payload));

            auto response = ggapi::Struct::create();
            response.put("status", true);
            return response;
        };
        CHECK(testScope1.subscribeToTopic(keys.publishToIoTCoreTopic, publishHandler1));

        SECTION("Single subscriber") {
            // start the lifecycle
            CHECK(sender.startLifecycle());

            // wait for the lifecycle to start
            std::this_thread::sleep_for(2s);

            SECTION("") {
                // check the publish response message
                auto pubMsg = sender.getPublishMessage();
                REQUIRE(pubMsg.hasKey("status"));
                REQUIRE(pubMsg.get<bool>("status"));
            }
            // stop lifecycle
            CHECK(sender.stopLifecycle());
        }

        SECTION("Multiple subscribers") {
            // create another subscriber
            auto testScope2 = ggapi::ModuleScope::registerGlobalPlugin(
                "test2", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });

            auto publishHandler2 = [topicName, qos, payload](ggapi::Task, ggapi::Symbol, ggapi::Struct request) {
                REQUIRE(request.hasKey(keys.topicName));
                REQUIRE(request.hasKey(keys.qos));
                REQUIRE(request.hasKey(keys.payload));

                REQUIRE_THAT(request.get<std::string>(keys.topicName), Equals(topicName));
                REQUIRE(request.get<int>(keys.qos) == 1);
                REQUIRE_THAT(request.get<std::string>(keys.payload), Equals(payload));

                auto response = ggapi::Struct::create();
                response.put("status", true);
                return response;
            };

            CHECK(testScope2.subscribeToTopic(keys.publishToIoTCoreTopic, publishHandler2));

            // start the lifecycle
            CHECK(sender.startLifecycle());

            // wait for the lifecycle to start
            std::this_thread::sleep_for(2s);

//            SECTION("") {
//                // check the publish response message
//                auto pubMsg = sender.getPublishMessage();
//                REQUIRE(pubMsg.hasKey("status"));
//                REQUIRE(pubMsg.get<bool>("status"));
//            }
            // stop lifecycle
            CHECK(sender.stopLifecycle());
        }
    }

    SECTION("Subscribe to Topic") {
        // create a publisher
        auto topicName1 = "ping/hello";
        auto payload1 = "Hello World!";

        auto testScope1 = ggapi::ModuleScope::registerGlobalPlugin(
            "test", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
        auto subscribeHandler1 = [topicName1, payload1](ggapi::Task, ggapi::Symbol, ggapi::Struct request) {
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
            message.put(keys.topicName, topicName1);
            message.put(keys.payload, payload1);
            auto statusResponse = ggapi::Task::sendToTopic(lpcResponseTopic, message);
            REQUIRE(statusResponse.hasKey("status"));
            REQUIRE(statusResponse.get<bool>("status"));
            return ggapi::Struct::create();
        };

        SECTION("Single Publisher") {
            CHECK(testScope1.subscribeToTopic(keys.subscribeToIoTCoreTopic, subscribeHandler1));

            // start the lifecycle
            CHECK(sender.startLifecycle());

            SECTION("Receive the published message") {
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

        SECTION("Multiple publishers to same topic") {

            auto topicName2 = topicName1;
            auto payload2 = "Sweet home!";

            auto testScope2 = ggapi::ModuleScope::registerGlobalPlugin(
                "test2", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });

            // create another publisher
            auto subscribeHandler2 = [topicName2, payload2](ggapi::Task, ggapi::Symbol, ggapi::Struct request) {
                REQUIRE(request.hasKey(keys.topicFilter));
                REQUIRE(request.hasKey(keys.qos));
                REQUIRE(request.hasKey(keys.lpcResponseTopic));

                auto lpcResponseTopic = request.get<std::string>(keys.lpcResponseTopic);
                REQUIRE_THAT(request.get<std::string>(keys.topicFilter), Equals("ping/#"));
                REQUIRE(request.get<int>(keys.qos) == 1);
                REQUIRE_THAT(lpcResponseTopic, Equals("mqttPing"));

                // fictitious publish message
                auto message = ggapi::Struct::create();
                message.put(keys.topicName, topicName2);
                message.put(keys.payload, payload2);
                auto statusResponse = ggapi::Task::sendToTopic(lpcResponseTopic, message);
                REQUIRE(statusResponse.hasKey("status"));
                REQUIRE(statusResponse.get<bool>("status"));
                return ggapi::Struct::create();
            };

            CHECK(testScope1.subscribeToTopic(keys.subscribeToIoTCoreTopic, subscribeHandler1));
            CHECK(testScope2.subscribeToTopic(keys.subscribeToIoTCoreTopic, subscribeHandler2));

            // start the lifecycle
            CHECK(sender.startLifecycle());

//            SECTION("Receive the first message") {
//                // check the subscribed message
//                auto subMsg1 = sender.getSubscribeMessage();
//                REQUIRE(subMsg1.hasKey(keys.topicName));
//                REQUIRE(subMsg1.hasKey(keys.payload));
//
//                REQUIRE_THAT(subMsg1.get<std::string>(keys.topicName), Equals(topicName1));
//                REQUIRE_THAT(subMsg1.get<std::string>(keys.payload), Equals(payload1));
//
//                // stop publishing to let the second message
//                testScope1.release();
//
//                SECTION("Receive the second message") {
//                    auto subMsg2 = sender.getSubscribeMessage();
//                    REQUIRE(subMsg2.hasKey(keys.topicName));
//                    REQUIRE(subMsg2.hasKey(keys.payload));
//
//                    REQUIRE_THAT(subMsg2.get<std::string>(keys.topicName), Equals(topicName2));
//                    REQUIRE_THAT(subMsg2.get<std::string>(keys.payload), Equals(payload2));
//                }
//            }

            // stop lifecycle
            CHECK(sender.stopLifecycle());
        }

        SECTION("Multiple publishers to different topics") {

            auto topicName2 = "ping/clock";
            auto payload2 = "Sweet home!";

            auto testScope2 = ggapi::ModuleScope::registerGlobalPlugin(
                "test2", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });

            // create another publisher
            auto subscribeHandler2 = [topicName2, payload2](ggapi::Task, ggapi::Symbol, ggapi::Struct request) {
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
                message.put(keys.topicName, topicName2);
                message.put(keys.payload, payload2);
                auto statusResponse = ggapi::Task::sendToTopic(lpcResponseTopic, message);
                REQUIRE(statusResponse.hasKey("status"));
                REQUIRE(statusResponse.get<bool>("status"));
                return ggapi::Struct::create();
            };

            CHECK(testScope1.subscribeToTopic(keys.subscribeToIoTCoreTopic, subscribeHandler1));
            CHECK(testScope2.subscribeToTopic(keys.subscribeToIoTCoreTopic, subscribeHandler2));

            // start the lifecycle
            CHECK(sender.startLifecycle());

//            SECTION("Receive the first message") {
//                // check the subscribed message
//                auto subMsg1 = sender.getSubscribeMessage();
//                REQUIRE(subMsg1.hasKey(keys.topicName));
//                REQUIRE(subMsg1.hasKey(keys.payload));
//
//                REQUIRE_THAT(subMsg1.get<std::string>(keys.topicName), Equals(topicName1));
//                REQUIRE_THAT(subMsg1.get<std::string>(keys.payload), Equals(payload1));
//
//                // stop publishing to let the second message
//                testScope1.release();
//
//                SECTION("Receive the second message") {
//                    auto subMsg2 = sender.getSubscribeMessage();
//                    REQUIRE(subMsg2.hasKey(keys.topicName));
//                    REQUIRE(subMsg2.hasKey(keys.payload));
//
//                    REQUIRE_THAT(subMsg2.get<std::string>(keys.topicName), Equals(topicName2));
//                    REQUIRE_THAT(subMsg2.get<std::string>(keys.payload), Equals(payload2));
//                }
//            }

            // stop lifecycle
            CHECK(sender.stopLifecycle());
        }
    }
}
