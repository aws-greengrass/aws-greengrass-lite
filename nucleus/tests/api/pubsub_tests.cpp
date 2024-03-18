#include "scope/context_full.hpp"
#include <catch2/catch_all.hpp>
#include <cpp_api.hpp>
#include <temp_module.hpp>

// NOLINTBEGIN

static ggapi::Struct simpleListener1(ggapi::StringOrd, ggapi::Container c) {
    ggapi::Struct s{c};
    s.put("=1", true);
    return s; // immediate return, set as value
}

static ggapi::Promise simpleListener2(ggapi::StringOrd, ggapi::Container c) {
    ggapi::Struct s{c};
    s.put("=2", true);
    return ggapi::Promise::of(s);
}

static ggapi::Future simpleListener3(ggapi::StringOrd, ggapi::Container c) {
    ggapi::Struct s{c};
    s.put("=3", true);
    // promise is fulfilled in the future, after 50ms delay
    // Note, pass s by parameter to ensure the callback mechanism keeps handle ref-count
    return ggapi::Promise::create().later(
        500, [](ggapi::Container cc, ggapi::Promise promise) { promise.setValue(cc); }, c);
}

static ggapi::Promise simpleListener4(ggapi::StringOrd, ggapi::Struct s) {
    s.put("=4", true);
    // promise is never fulfilled (requires wait timeout)
    return ggapi::Promise::create();
}

SCENARIO("PubSub API", "[pubsub]") {
    scope::LocalizedContext forTesting{};
    auto context = forTesting.context()->context();
    util::TempModule testModule("pubsub-test");

    GIVEN("Some listeners") {
        ggapi::Subscription subs1{ggapi::Subscription::subscribeToTopic({}, simpleListener1)};
        ggapi::Subscription subs2{
            ggapi::Subscription::subscribeToTopic("some-topic", simpleListener2)};
        ggapi::Subscription subs3{
            ggapi::Subscription::subscribeToTopic("some-topic", simpleListener3)};
        WHEN("Calling by topic") {
            auto data = ggapi::Struct::create();
            auto promises = ggapi::Subscription::callTopicAll("some-topic", data);
            THEN("Topic listeners were visited") {
                REQUIRE_FALSE(data.hasKey("=1"));
                REQUIRE(data.hasKey("=2"));
                REQUIRE(data.hasKey("=3"));
                REQUIRE(promises.size() == 2);
                REQUIRE(promises.ready() == 1);
                promises.waitAll();
                REQUIRE(promises.ready() == 2);
                REQUIRE(promises[0].getValue().isSameObject(data));
                REQUIRE(promises[1].getValue().isSameObject(data));
            }
        }
        WHEN("Calling by handle") {
            auto data = ggapi::Struct::create();
            auto future = subs1.call(data);
            THEN("Single listener was visited") {
                REQUIRE(data.hasKey("=1"));
                REQUIRE_FALSE(data.hasKey("=2"));
                REQUIRE_FALSE(data.hasKey("=3"));
            }
            THEN("Promise was fulfilled") {
                future.wait();
                REQUIRE(future.getValue().isSameObject(data));
            }
        }
    }
}

// NOLINTEND
