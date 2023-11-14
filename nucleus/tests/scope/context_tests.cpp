#include "scope/context_full.hpp"
#include <catch2/catch_all.hpp>

// NOLINTBEGIN

scope::Context *pAltCtx = nullptr;
scope::ThreadContext *pAltThreadCtx = nullptr;
void altThread() {
    pAltCtx = &scope::context();
    pAltThreadCtx = &scope::thread();
    auto &x = scope::ThreadContextContainer::perThread();
}
void altThread2(std::shared_ptr<scope::Context> other) {
    scope::thread().changeContext(other);
    pAltCtx = &scope::context();
    pAltThreadCtx = &scope::thread();
}

SCENARIO("Context behavior", "[context]") {
    GIVEN("No override of context") {
        WHEN("Default context is accessed") {
            auto &context1 = scope::context();
            auto &context2 = scope::context();
            THEN("Context is not null") {
                REQUIRE(&context1 != nullptr);
            }
            THEN("Contexts are returned consistently") {
                REQUIRE(&context1 == &context2);
            }
        }
        WHEN("Per thread context is accessed in main thread") {
            auto &threadCtx1 = scope::thread();
            auto &threadCtx2 = scope::thread();
            THEN("Context is not null") {
                REQUIRE(&threadCtx1 != nullptr);
            }
            THEN("Contexts are returned consistently") {
                REQUIRE(&threadCtx1 == &threadCtx2);
            }
        }
        WHEN("Another thread is created") {
            auto &context1 = scope::context();
            auto &threadCtx1 = scope::thread();
            auto thread = std::thread(altThread);
            thread.join();
            THEN("Other context is not null") {
                REQUIRE(pAltCtx != nullptr);
            }
            THEN("Context is the same") {
                REQUIRE(&context1 == pAltCtx);
            }
            THEN("Other thread context is not null") {
                REQUIRE(pAltThreadCtx != nullptr);
            }
            THEN("Thread Contexts are unique") {
                REQUIRE(&threadCtx1 != pAltThreadCtx);
            }
        }
    }
    GIVEN("Context is overridden for testing") {
        auto &defContext = scope::context();
        auto &defThreadCtx = scope::thread();
        scope::LocalizedScope forTesting{scope::Context::create()};
        WHEN("Contexts are accessed") {
            auto &context2 = scope::context();
            auto &threadCtx2 = scope::thread();
            THEN("New contexts are not null") {
                REQUIRE(&context2 != nullptr);
                REQUIRE(&threadCtx2 != nullptr);
            }
            THEN("New contexts are unique") {
                REQUIRE(&context2 != &defContext);
                REQUIRE(&threadCtx2 != &defThreadCtx);
            }
        }
        WHEN("Another thread is created") {
            auto &context1 = scope::context();
            auto thread = std::thread(altThread);
            thread.join();
            THEN("Thread uses default context") {
                REQUIRE(pAltCtx != &context1);
                REQUIRE(pAltCtx == &defContext);
            }
        }
        WHEN("Another thread is associated with new context") {
            auto &context1 = scope::context();
            auto &threadCtx1 = scope::thread();
            auto thread = std::thread(altThread2, context1.baseRef());
            thread.join();
            THEN("Thread uses same context") {
                REQUIRE(pAltCtx == &context1);
            }
            THEN("Thread context is still different") {
                REQUIRE(pAltThreadCtx != &threadCtx1);
            }
        }
    }
}

// NOLINTEND
