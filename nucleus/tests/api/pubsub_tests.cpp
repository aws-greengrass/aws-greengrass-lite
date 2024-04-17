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
    // promise is fulfilled in the future, after delay
    return ggapi::Promise::create().later(
        500,
        // This callback is simply deferred
        [](ggapi::Container cc, ggapi::Promise promise) {
            promise.fulfill(
                // This callback converts return value/exception into a promise
                [](auto &cc) { return cc; },
                cc);
        },
        c);
}

static ggapi::Future simpleListener4ImmediateError(ggapi::StringOrd, ggapi::Container c) {
    ggapi::Struct s{c};
    s.put("=4", true);
    throw std::runtime_error("=4");
}

static ggapi::Future simpleListener5DeferredError(ggapi::StringOrd, ggapi::Container c) {
    ggapi::Struct s{c};
    s.put("=5", true);
    // promise is fulfilled in the future, after delay
    return ggapi::Promise::create().later(
        500,
        // This callback is simply deferred
        [](ggapi::Container cc, ggapi::Promise promise) {
            promise.fulfill(
                // This callback converts return value/exception into a promise
                [](auto &cc) -> ggapi::Container { throw std::runtime_error("=5"); },
                cc);
        },
        c);
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

    GIVEN("Some listeners that fail") {
        ggapi::Subscription subs2{
            ggapi::Subscription::subscribeToTopic("some-topic", simpleListener2)};
        ggapi::Subscription subs3{
            ggapi::Subscription::subscribeToTopic("some-topic", simpleListener3)};
        ggapi::Subscription subs4{
            ggapi::Subscription::subscribeToTopic("some-topic", simpleListener4ImmediateError)};
        ggapi::Subscription subs5{
            ggapi::Subscription::subscribeToTopic("some-topic", simpleListener5DeferredError)};
        WHEN("Calling by topic") {
            auto data = ggapi::Struct::create();
            auto promises = ggapi::Subscription::callTopicAll("some-topic", data);
            THEN("All topic listeners were visited") {
                REQUIRE(data.hasKey("=2"));
                REQUIRE(data.hasKey("=3"));
                REQUIRE(data.hasKey("=4"));
                REQUIRE(data.hasKey("=5"));
                REQUIRE(promises.size() == 4);
                REQUIRE(promises.ready() == 2);
                promises.waitAll();
                REQUIRE(promises.ready() == 4);
                REQUIRE_THROWS_AS(promises[0].getValue(), std::runtime_error);
                REQUIRE_THROWS_AS(promises[1].getValue(), std::runtime_error);
                REQUIRE(promises[2].getValue().isSameObject(data));
                REQUIRE(promises[3].getValue().isSameObject(data));
            }
        }
    }
}

// NOLINTEND
