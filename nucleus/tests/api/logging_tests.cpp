#include "logging/log_queue.hpp"
#include "scope/context_full.hpp"
#include <catch2/catch_all.hpp>
#include <cpp_api.hpp>

SCENARIO("Basic use of logging", "[logging]") {
    GIVEN("A log context") {

        const auto LOG = // NOLINT(cert-err58-cpp)
            ggapi::Logger::of("Logging");

        auto &logManager = scope::context().logManager();
        logging::LogQueue::QueueEntry lastEntry;
        logManager.publishQueue()->setWatch([&lastEntry](auto entry) {
            lastEntry = entry;
            return false;
        });
        WHEN("Logging a simple event at error") {
            LOG.atError().event("log-event").kv("key", "value").log("message");
            logManager.publishQueue()->drainQueue();
        }
    }
}
