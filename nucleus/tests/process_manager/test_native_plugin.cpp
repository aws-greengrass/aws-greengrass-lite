#include "test_util.hpp"
#include <catch2/catch_all.hpp>
#include <type_traits>

#include <native_plugin.hpp>

SCENARIO("Native plugin characteristics", "[native]") {
    GIVEN("Native plugin static interface") {
        THEN("Type traits") {
            STATIC_CHECK(std::is_default_constructible_v<NativePlugin>);
            STATIC_CHECK(!std::is_copy_constructible_v<NativePlugin>);
            STATIC_CHECK(!std::is_copy_assignable_v<NativePlugin>);
            STATIC_CHECK(!std::is_move_constructible_v<NativePlugin>);
            STATIC_CHECK(!std::is_move_assignable_v<NativePlugin>);
        }
    }

    GIVEN("Constructors") {
        auto &sender1 = NativePlugin::get();
        auto &sender2 = NativePlugin::get();
        THEN("Both instances are same") {
            REQUIRE(&sender1 == &sender2);
        }
    }
}
