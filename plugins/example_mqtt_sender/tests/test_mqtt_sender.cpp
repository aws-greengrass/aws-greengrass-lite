#include "test_util.hpp"
#include <catch2/catch_all.hpp>
#include <type_traits>

TEST_CASE("Example Mqtt Sender plugin characteristics", "[pubsub]") {
    SECTION("Type traits") {
        STATIC_CHECK(std::is_default_constructible_v<ExampleMqttSender>);
        STATIC_CHECK(!std::is_copy_constructible_v<ExampleMqttSender>);
        STATIC_CHECK(!std::is_copy_assignable_v<ExampleMqttSender>);
        STATIC_CHECK(!std::is_move_constructible_v<ExampleMqttSender>);
        STATIC_CHECK(!std::is_move_assignable_v<ExampleMqttSender>);
    }

    SECTION("Constructors") {
        auto &sender1 = ExampleMqttSender::get();
        auto &sender2 = ExampleMqttSender::get();
        REQUIRE(&sender1 == &sender2);
    }

    SECTION("Complete lifecycle") {
        auto moduleScope = ggapi::ModuleScope::registerGlobalPlugin(
            "test", [](ggapi::ModuleScope, ggapi::Symbol, ggapi::Struct) { return false; });
        TestExampleMqttSender sender = TestExampleMqttSender(moduleScope);
        REQUIRE(!sender.executePhase(BOOTSTRAP));
        REQUIRE(!sender.executePhase(BIND));
        REQUIRE(!sender.executePhase(DISCOVER));
        REQUIRE(sender.executePhase(START));
        REQUIRE(sender.executePhase(RUN));
        // wait for the lifecycle to start
        std::this_thread::sleep_for(1s);
        REQUIRE(sender.executePhase(TERMINATE));
        // wait for the lifecycle to stop
        std::this_thread::sleep_for(1s);
    }
}
