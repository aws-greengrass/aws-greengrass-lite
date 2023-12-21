#include "test_util.hpp"
#include <catch2/catch_all.hpp>
#include <type_traits>

SCENARIO("Example Mqtt Sender plugin characteristics", "[pubsub]") {
    GIVEN("Example Mqtt Sender plugin") {
        THEN("Type traits") {
            STATIC_CHECK(std::is_default_constructible_v<ExampleMqttSender>);
            STATIC_CHECK(!std::is_copy_constructible_v<ExampleMqttSender>);
            STATIC_CHECK(!std::is_copy_assignable_v<ExampleMqttSender>);
            STATIC_CHECK(!std::is_move_constructible_v<ExampleMqttSender>);
            STATIC_CHECK(!std::is_move_assignable_v<ExampleMqttSender>);
        }
    }

    GIVEN("Constructors") {
        auto &sender1 = ExampleMqttSender::get();
        auto &sender2 = ExampleMqttSender::get();
        THEN("Both instances are same") {
            REQUIRE(&sender1 == &sender2);
        }
    }

    GIVEN("Complete lifecycle") {
        auto moduleScope = ggapi::ModuleScope::registerGlobalPlugin(
            "module", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
        TestExampleMqttSender sender = TestExampleMqttSender(moduleScope);
        THEN("All phases are executed") {
            REQUIRE(!sender.executePhase(BOOTSTRAP));
            REQUIRE(!sender.executePhase(BIND));
            REQUIRE(!sender.executePhase(DISCOVER));
            REQUIRE(sender.executePhase(START));
//             TODO: Fix causing race
            REQUIRE(sender.executePhase(RUN));
            REQUIRE(sender.executePhase(TERMINATE));
        }
    }
}
