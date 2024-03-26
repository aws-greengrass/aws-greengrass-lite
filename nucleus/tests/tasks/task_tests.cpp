#include "data/shared_struct.hpp"
#include "scope/context_full.hpp"
#include "tasks/expire_time.hpp"
#include "tasks/task.hpp"
#include "tasks/task_threads.hpp"
#include <catch2/catch_all.hpp>
#include <chrono>

// NOLINTBEGIN
namespace constants {
    using namespace std::chrono_literals;
    static constexpr auto TIMER_GRANULARITY = 500ms; // If too small, tests will become unstable
} // namespace constants

struct TestTask : public tasks::Task {
    std::shared_ptr<pubsub::Promise> promise;
    std::shared_ptr<data::ContainerModelBase> data;
    tasks::ExpireTime fired{tasks::ExpireTime::epoch()};

    TestTask(std::shared_ptr<pubsub::Promise> p, std::shared_ptr<data::ContainerModelBase> d)
        : promise(p), data(d) {
    }

    void invoke() override {
        fired = tasks::ExpireTime::now();
        promise->setValue(data);
    }
};

SCENARIO("Task management", "[tasks]") {
    scope::LocalizedContext forTesting{scope::Context::create()};
    auto context = forTesting.context()->context();
    auto &taskManager = context->taskManager();

    GIVEN("An simple queued task") {
        auto promise = std::make_shared<pubsub::Promise>(context);
        auto data = data::Boxed::box(context, true);
        auto task = std::make_shared<TestTask>(promise, data);
        taskManager.queueTask(task);

        WHEN("Wait for completion") {
            auto exp = tasks::ExpireTime::fromNowMillis(500);
            bool didComplete = promise->waitUntil(exp);
            THEN("Task returns completed") {
                REQUIRE(didComplete);
            }
            THEN("Task returns data") {
                REQUIRE(promise->getValue() == data);
            }
        }
    }
}

SCENARIO("Deferred task management", "[tasks]") {
    scope::LocalizedContext forTesting{scope::Context::create()};
    auto context = forTesting.context()->context();
    auto &taskManager = context->taskManager();

    GIVEN("A set of three deferred tasks") {
        auto data1 = data::Boxed::box(context, 1);
        auto data2 = data::Boxed::box(context, 2);
        auto data3 = data::Boxed::box(context, 3);
        auto promise1 = std::make_shared<pubsub::Promise>(context);
        auto promise2 = std::make_shared<pubsub::Promise>(context);
        auto promise3 = std::make_shared<pubsub::Promise>(context);
        auto task1{std::make_shared<TestTask>(promise1, data1)};
        auto task2{std::make_shared<TestTask>(promise2, data2)};
        auto task3{std::make_shared<TestTask>(promise3, data3)};
        auto start = tasks::ExpireTime::now();
        auto task1Time = start + constants::TIMER_GRANULARITY * 2;
        auto task1MaxTime = start + constants::TIMER_GRANULARITY * 3;
        auto task2Time = start + constants::TIMER_GRANULARITY * 4;
        auto task2MaxTime = start + constants::TIMER_GRANULARITY * 5;
        auto task3Time = start + constants::TIMER_GRANULARITY * 6;
        auto task3MaxTime = start + constants::TIMER_GRANULARITY * 7;
        auto maxTime = start + constants::TIMER_GRANULARITY * 8;
        // scheduled out of order intentionally
        taskManager.queueTask(task3, task3Time);
        taskManager.queueTask(task1, task1Time);
        taskManager.queueTask(task2, task2Time);

        WHEN("Waiting for all three tasks completed") {
            // waiting out of order intentionally
            bool didComplete2 = promise2->waitUntil(maxTime);
            bool didComplete3 = promise3->waitUntil(maxTime);
            bool didComplete1 = promise1->waitUntil(maxTime);
            THEN("Tasks did complete") {
                REQUIRE(didComplete1);
                REQUIRE(didComplete2);
                REQUIRE(didComplete3);
                AND_THEN("Tasks completed in correct order") {
                    const uint64_t task1Millis = task1->fired.asMilliseconds();
                    const uint64_t task2Millis = task2->fired.asMilliseconds();
                    const uint64_t task3Millis = task3->fired.asMilliseconds();
                    REQUIRE(task1Millis > start.asMilliseconds());
                    REQUIRE(task2Millis > task1Millis);
                    REQUIRE(task3Millis > task2Millis);
                    AND_THEN("Tasks were delayed") {
                        REQUIRE(task1Millis >= task1Time.asMilliseconds());
                        REQUIRE(task2Millis >= task2Time.asMilliseconds());
                        REQUIRE(task3Millis >= task3Time.asMilliseconds());
                    }
                    AND_THEN("Tasks did not take too long") {
                        REQUIRE(task1Millis < task1MaxTime.asMilliseconds());
                        REQUIRE(task2Millis < task2MaxTime.asMilliseconds());
                        REQUIRE(task3Millis < task3MaxTime.asMilliseconds());
                    }
                }
            }
        }
    }
}

// NOLINTEND
