#include "scope/context_full.hpp"
#include <catch2/catch_all.hpp>
#include <cpp_api.hpp>
#include <temp_module.hpp>

// NOLINTBEGIN

static void simpleCallback(ggapi::Struct s, ggapi::Promise p) {
    s.put("=1", true);
    p.setValue(s);
}

SCENARIO("Deferred Tasks", "[tasks]") {
    scope::LocalizedContext forTesting{};
    auto context = forTesting.context()->context();
    util::TempModule testModule("task-test");

    GIVEN("A simple task callback") {
        WHEN("Calling a deferred function") {
            auto data = ggapi::Struct::create();
            ggapi::Future f = ggapi::Promise::create().async(simpleCallback, data);
            auto success = f.wait(500);
            THEN("Wait did not time out") {
                REQUIRE(success);
            }
            THEN("Value was returned") {
                REQUIRE(f.getValue().isSameObject(data));
            }
            THEN("Handle reanchoring occurred") {
                REQUIRE(f.getValue() != data);
            }
            THEN("Callback was visited") {
                REQUIRE(data.hasKey("=1"));
            }
        }
    }
}

// NOLINTEND
