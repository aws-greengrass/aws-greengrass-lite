#include <catch2/catch_all.hpp>
#include <cpp_api.hpp>

// NOLINTBEGIN

static ggapi::Struct simpleListener1(ggapi::Scope, ggapi::StringOrd, ggapi::Struct s) {
    s.put("=1", true);
    return ggapi::Struct{0};
}

static ggapi::Struct simpleListener2(ggapi::Scope, ggapi::StringOrd, ggapi::Struct s) {
    s.put("=2", true);
    return ggapi::Struct{0};
}

static ggapi::Struct simpleListener3(ggapi::Scope, ggapi::StringOrd, ggapi::Struct s) {
    s.put("=3", true);
    return ggapi::Struct{0};
}

SCENARIO("PubSub API", "[pubsub]") {
    auto scope = ggapi::ThreadScope::claimThread();
    GIVEN("Some listeners") {
        ggapi::Subscription handle1{scope.subscribeToTopic({}, simpleListener1)};
        ggapi::Subscription handle2{scope.subscribeToTopic("some-topic", simpleListener2)};
        (void) scope.subscribeToTopic("some-topic", simpleListener3);
        WHEN("Calling by topic") {
            ggapi::Struct data = scope.createStruct();
            (void) scope.sendToTopic("some-topic", data);
            THEN("Topic listeners were visited") {
                REQUIRE_FALSE(data.hasKey("=1"));
                REQUIRE(data.hasKey("=2"));
                REQUIRE(data.hasKey("=3"));
            }
        }
        WHEN("Calling by handle") {
            ggapi::Struct data = scope.createStruct();
            (void) scope.sendToListener(handle1, data);
            THEN("Single listener was visited") {
                REQUIRE(data.hasKey("=1"));
                REQUIRE_FALSE(data.hasKey("=2"));
                REQUIRE_FALSE(data.hasKey("=3"));
            }
        }
        WHEN("Calling topic listener by handle") {
            ggapi::Struct data = scope.createStruct();
            (void) scope.sendToListener(handle2, data);
            THEN("Single listener was visited") {
                REQUIRE_FALSE(data.hasKey("=1"));
                REQUIRE(data.hasKey("=2"));
                REQUIRE_FALSE(data.hasKey("=3"));
            }
        }
    }
}

// NOLINTEND
