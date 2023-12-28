#include "test_util.hpp"
#include <catch2/catch_all.hpp>
#include <type_traits>

SCENARIO("Example Mqtt Sender plugin characteristics", "[pubsub]") {
    GIVEN("Example Mqtt Sender plugin") {
        THEN("Type traits") {
            STATIC_CHECK(std::is_default_constructible_v<MqttSender>);
            STATIC_CHECK(!std::is_copy_constructible_v<MqttSender>);
            STATIC_CHECK(!std::is_copy_assignable_v<MqttSender>);
            STATIC_CHECK(!std::is_move_constructible_v<MqttSender>);
            STATIC_CHECK(!std::is_move_assignable_v<MqttSender>);
        }
    }

    GIVEN("Constructors") {
        auto &sender1 = MqttSender::get();
        auto &sender2 = MqttSender::get();
        THEN("Both instances are same") {
            REQUIRE(&sender1 == &sender2);
        }
    }

    GIVEN("Complete lifecycle") {
        auto moduleScope = ggapi::ModuleScope::registerGlobalPlugin(
            "module", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
        TestMqttSender sender = TestMqttSender(moduleScope);
        THEN("All phases are executed") {
            //             TODO: Fix causing race
//            REQUIRE(!sender.executePhase(BOOTSTRAP));
//            REQUIRE(!sender.executePhase(BIND));
//            REQUIRE(!sender.executePhase(DISCOVER));
//            REQUIRE(sender.executePhase(START));
//            REQUIRE(sender.executePhase(RUN));
//            REQUIRE(sender.executePhase(TERMINATE));
        }
    }
}
