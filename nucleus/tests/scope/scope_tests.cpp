#include "scope/context_full.hpp"
#include <catch2/catch_all.hpp>

// NOLINTBEGIN

SCENARIO("Nucleus call scope", "[context][scope]") {
    GIVEN("A context") {
        scope::LocalizedContext forTesting{scope::Context::create()};

        WHEN("No scope explicitly created") {
            THEN("A default scope is present") {
                auto callScope1 = scope::thread()->getCallScope();
                REQUIRE(callScope1.get() != nullptr);
            }
            THEN("A default scope obtained multiple times is the same") {
                auto callScope1 = scope::thread()->getCallScope();
                auto callScope2 = scope::thread()->getCallScope();
                REQUIRE(callScope1 == callScope2);
            }
        }

        WHEN("A nested stack scope is created") {
            auto callScope1 = scope::thread()->getCallScope(); // before nesting
            auto nestedStackScope = scope::StackScope{};
            THEN("A nested stack scope is present") {
                auto callScope2 = scope::thread()->getCallScope();
                REQUIRE(callScope2.get() != nullptr);
            }
            THEN("Nested stack scope results in a different call scope") {
                auto callScope2 = scope::thread()->getCallScope();
                REQUIRE(callScope1 != callScope2);
            }
            THEN("A nested stack scope object can get call scope") {
                auto callScope2 = scope::thread()->getCallScope();
                auto callScope3 = nestedStackScope.getCallScope();
                REQUIRE(callScope2 == callScope3);
            }
            AND_WHEN("A nested stack scope is released") {
                std::weak_ptr<scope::CallScope> callScope2 = scope::thread()->getCallScope();
                nestedStackScope.release();
                THEN("Nested stack scope is deleted") {
                    REQUIRE(callScope2.expired());
                }
                THEN("Call scope is effectively popped") {
                    auto callScope3 = scope::thread()->getCallScope();
                    REQUIRE(callScope1 == callScope3);
                }
            }
            AND_WHEN("A plugin call scope is created") {
                auto callScope2 = nestedStackScope.getCallScope();
                ggapi::CallScope pluginCallScope;
                THEN("Yet another call scope is created") {
                    auto callScope4 = scope::thread()->getCallScope();
                    REQUIRE(callScope4 != callScope1);
                    REQUIRE(callScope4 != callScope2);
                    REQUIRE(callScope4 == nestedStackScope.getCallScope());
                }
                THEN("Handle is references the same call scope") {
                    auto callScope4 = scope::thread()->getCallScope();
                    auto byHandle =
                        scope::context()
                            ->handles()
                            .apply(data::ObjHandle::Partial{pluginCallScope.getHandleId()})
                            .toObject();
                    REQUIRE(byHandle.get() != nullptr);
                    REQUIRE(byHandle == callScope4);
                }
                AND_WHEN("Nested stack scope is released") {
                    nestedStackScope.release();
                    THEN("Call scope is effectively popped to that before nested stack scope") {
                        auto callScope5 = scope::thread()->getCallScope();
                        REQUIRE(callScope1 == callScope5);
                    }
                }
            }
            AND_WHEN("CallScope is called and released") {
                auto callScope10 = scope::thread()->getCallScope();
                std::shared_ptr<scope::CallScope> callScope11;
                {
                    ggapi::CallScope callScope;
                    callScope11 = scope::thread()->getCallScope();
                }
                auto callScope12 = scope::thread()->getCallScope();
                THEN("Call scope was popped correctly") {
                    REQUIRE(callScope10 == callScope12);
                }
                THEN("A nested callscope was created") {
                    REQUIRE(callScope10 != callScope11);
                }
            }
        }
    }
}

// NOLINTEND
