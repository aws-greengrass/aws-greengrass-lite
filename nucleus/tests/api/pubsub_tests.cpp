#include "scope/context_full.hpp"
#include <catch2/catch_all.hpp>
#include <cpp_api.hpp>

// NOLINTBEGIN

static ggapi::Struct simpleListener1(ggapi::Task, ggapi::StringOrd, ggapi::Struct s) {
    s.put("=1", true);
    return ggapi::Struct{0};
}

static ggapi::Struct simpleListener2(ggapi::Task, ggapi::StringOrd, ggapi::Struct s) {
    s.put("=2", true);
    return ggapi::Struct{0};
}

static ggapi::Struct simpleListener3(ggapi::Task, ggapi::StringOrd, ggapi::Struct s) {
    s.put("=3", true);
    return ggapi::Struct{0};
}

SCENARIO("PubSub API", "[pubsub]") {
    scope::LocalizedContext forTesting{scope::Context::create()};
    ggapi::CallScope callScope{};

    GIVEN("Some listeners") {
        ggapi::Subscription subs1{callScope.subscribeToTopic({}, simpleListener1)};
        ggapi::Subscription subs2{callScope.subscribeToTopic("some-topic", simpleListener2)};
        (void) callScope.subscribeToTopic("some-topic", simpleListener3);
        WHEN("Calling by topic") {
            auto data = ggapi::Struct::create();
            (void) ggapi::Task::sendToTopic("some-topic", data);
            THEN("Topic listeners were visited") {
                REQUIRE_FALSE(data.hasKey("=1"));
                REQUIRE(data.hasKey("=2"));
                REQUIRE(data.hasKey("=3"));
            }
        }
        WHEN("Calling by handle") {
            auto data = ggapi::Struct::create();
            (void) subs1.call(data);
            THEN("Single listener was visited") {
                REQUIRE(data.hasKey("=1"));
                REQUIRE_FALSE(data.hasKey("=2"));
                REQUIRE_FALSE(data.hasKey("=3"));
            }
        }
        WHEN("Calling topic listener by handle") {
            auto data = ggapi::Struct::create();
            (void) subs2.call(data);
            THEN("Single listener was visited") {
                REQUIRE_FALSE(data.hasKey("=1"));
                REQUIRE(data.hasKey("=2"));
                REQUIRE_FALSE(data.hasKey("=3"));
            }
        }
    }
}

// NOLINTEND
