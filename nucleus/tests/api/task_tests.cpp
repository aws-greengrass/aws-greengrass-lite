#include "scope/context_full.hpp"
#include <catch2/catch_all.hpp>
#include <cpp_api.hpp>

// NOLINTBEGIN

static void simpleCallback(ggapi::Struct s) {
    s.put("=1", true);
}

SCENARIO("Deferred Tasks", "[tasks]") {
    scope::LocalizedContext forTesting{scope::Context::create()};
    ggapi::CallScope callScope{};

    GIVEN("A simple task callback") {
        WHEN("Calling a deferred function") {
            auto data = ggapi::Struct::create();
            ggapi::Task t = ggapi::Task::callAsync(data, simpleCallback, 0);
            std::ignore = t.waitForTaskCompleted(100);
            THEN("Callback was visited") {
                REQUIRE(data.hasKey("=1"));
            }
        }
    }
}

// NOLINTEND
