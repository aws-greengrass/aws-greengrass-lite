#include "scope/context_full.hpp"
#include <catch2/catch_all.hpp>
#include <cpp_api.hpp>
#include <temp_module.hpp>

// NOLINTBEGIN

SCENARIO("Module scopes", "[module]") {
    scope::LocalizedContext forTesting{};
    auto context = forTesting.context()->context();

    GIVEN("a module with a handle") {
        util::TempModule testModule("pubsub-test");
        WHEN("Creating a handle") {
            auto data = ggapi::Struct::create();
            THEN("Handle is valid") {
                REQUIRE(data.isStruct());
            }
            AND_WHEN("Nesting and releasing a module") {
                util::TempModule copyModule(*testModule);
                copyModule.release();
                THEN("Module handle is still valid") {
                    REQUIRE(testModule->isScope());
                }
                THEN("Nested handle is still valid") {
                    REQUIRE(data.isStruct());
                }
                AND_WHEN("Nesting and releasing a module again") {
                    util::TempModule copyModule2(*testModule);
                    copyModule2.release();
                    THEN("Module handle is still valid") {
                        REQUIRE(testModule->isScope());
                    }
                    THEN("Nested handle is still valid") {
                        REQUIRE(data.isStruct());
                    }
                }
            }
            AND_WHEN("Cloning a handle") {
                auto holder = ggapi::List::create();
                holder.append(data);
                auto copy = holder.get<ggapi::Struct>(0);
                holder.reset();
                THEN("Clone handle is different to original") {
                    REQUIRE(copy != data);
                }
                AND_WHEN("Resetting original handle") {
                    data.reset();
                    THEN("Copy is still valid") {
                        REQUIRE(copy.isStruct());
                    }
                }
            }
        }
    }
}

// NOLINTEND
