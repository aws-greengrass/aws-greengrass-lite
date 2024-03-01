#include "logging/log_queue.hpp"
#include "scope/context_full.hpp"
#include <catch2/catch_all.hpp>
#include <cpp_api.hpp>
#include <temp_module.hpp>

// NOLINTBEGIN

SCENARIO("Basic use of logging", "[logging]") {

    util::TempModule testModule("logging-test");

    GIVEN("A log context") {

        const auto LOG = // NOLINT(cert-err58-cpp)
            ggapi::Logger::of("Logging");

        auto ctx = scope::context();
        auto &logManager = ctx->logManager();
        logging::LogQueue::QueueEntry lastEntry;
        logManager.publishQueue()->setWatch([&lastEntry](auto entry) {
            lastEntry = entry;
            return false;
        });
        WHEN("Logging a simple event at error") {
            LOG.atError().event("log-event").kv("key", "value").log("message");
            logManager.publishQueue()->drainQueue();
            THEN("Log entry was filled with event details") {
                auto data = lastEntry.second;
                REQUIRE(data->get("event").getString() == "log-event");
                // TODO: More assertions and tests needed
            }
        }
    }
}

// NOLINTEND
