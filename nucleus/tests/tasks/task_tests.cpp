#include "data/environment.hpp"
#include "data/shared_struct.hpp"
#include "tasks/task.hpp"
#include "tasks/task_manager.hpp"
#include "tasks/task_threads.hpp"
#include <catch2/catch_all.hpp>

// NOLINTBEGIN
static constexpr auto TIMER_GRANULARITY{200}; // If too small, tests will become unstable

class SubTaskStub : public tasks::SubTask {
    std::string _flagName;
    std::shared_ptr<data::StructModelBase> _returnData;

public:
    SubTaskStub(
        const std::string_view &flagName, const std::shared_ptr<data::StructModelBase> &returnData
    )
        : _flagName(flagName), _returnData{returnData} {
    }

    SubTaskStub(const std::string_view &flagName) : _flagName(flagName) {
    }

    std::shared_ptr<data::StructModelBase> runInThread(
        const std::shared_ptr<tasks::Task> &task,
        const std::shared_ptr<data::StructModelBase> &dataIn
    ) override {
        if(dataIn) {
            dataIn->put(_flagName, data::StructElement{true});
        }
        if(_returnData) {
            _returnData->put(
                "_" + _flagName,
                data::StructElement{std::static_pointer_cast<data::ContainerModelBase>(dataIn)}
            );
            _returnData->put("$" + _flagName, tasks::ExpireTime::now().asMilliseconds());
        }
        return _returnData;
    }
};

SCENARIO("Task management", "[tasks]") {
    data::Environment environment;
    std::shared_ptr<tasks::TaskManager> taskManager{
        std::make_shared<tasks::TaskManager>(environment)};
    tasks::FixedTaskThreadScope threadScope{
        std::make_shared<tasks::FixedTaskThread>(environment, taskManager)};

    GIVEN("An empty task") {
        auto taskAnchor{taskManager->createTask()};
        auto task{taskAnchor.getObject<tasks::Task>()};
        auto taskInitData{std::make_shared<data::SharedStruct>(environment)};
        task->setData(taskInitData);
        taskManager->queueTask(task);
        // note, for this test, no worker allocated

        WHEN("Polling once for completion") {
            tasks::ExpireTime expireTime = tasks::ExpireTime::now();
            bool didComplete = task->waitForCompletion(expireTime);
            auto data = task->getData();
            THEN("Task returns completed") {
                REQUIRE(didComplete);
            }
            THEN("Task returns no data") {
                REQUIRE(data == nullptr);
            }
        }
    }

    GIVEN("An empty task with completion function") {

        auto taskAnchor{taskManager->createTask()};
        auto task{taskAnchor.getObject<tasks::Task>()};
        auto taskInitData{std::make_shared<data::SharedStruct>(environment)};
        auto taskCompData{std::make_shared<data::SharedStruct>(environment)};
        auto finalize{std::make_unique<SubTaskStub>("comp", taskCompData)};
        task->setData(taskInitData);
        task->setCompletion(std::move(finalize));
        taskManager->queueTask(task);
        // note, for this test, no worker allocated

        WHEN("Polling once for completion") {
            tasks::ExpireTime expireTime = tasks::ExpireTime::now();
            bool didComplete = task->waitForCompletion(expireTime);
            auto data = task->getData();
            THEN("Task returns completed") {
                REQUIRE(didComplete);
            }
            THEN("Task completion received no data") {
                REQUIRE(taskCompData->hasKey("_comp"));
                auto compStruct = taskCompData->get("_comp").castObject<data::SharedStruct>();
                REQUIRE(compStruct == nullptr);
            }
            THEN("Task returned no data") {
                REQUIRE(data == nullptr);
            }
        }
    }

    GIVEN("A single non-return sub-task with completion function") {
        auto taskAnchor{taskManager->createTask()};
        auto task{taskAnchor.getObject<tasks::Task>()};
        auto taskInitData{std::make_shared<data::SharedStruct>(environment)};
        auto taskCompData{std::make_shared<data::SharedStruct>(environment)};
        auto subTask1{std::make_unique<SubTaskStub>("subTask1")};
        auto finalize{std::make_unique<SubTaskStub>("comp", taskCompData)};
        task->setData(taskInitData);
        task->addSubtask(std::move(subTask1));
        task->setCompletion(std::move(finalize));
        taskManager->queueTask(task);
        // note, for this test, no worker allocated

        WHEN("Polling once for completion") {
            tasks::ExpireTime expireTime = tasks::ExpireTime::now();
            bool didComplete = task->waitForCompletion(expireTime);
            auto data = task->getData();
            THEN("Task returns completed") {
                REQUIRE(didComplete);
            }
            THEN("Subtask1 was visited") {
                REQUIRE(taskInitData->hasKey("subTask1"));
            }
            THEN("Task completion received no data") {
                REQUIRE(taskCompData->hasKey("_comp"));
                auto compStruct = taskCompData->get("_comp").castObject<data::SharedStruct>();
                REQUIRE(compStruct == nullptr);
            }
            THEN("Task returned no data") {
                REQUIRE(data == nullptr);
            }
        }
    }

    GIVEN("Three sub-tasks with completion function") {
        auto taskAnchor{taskManager->createTask()};
        auto task{taskAnchor.getObject<tasks::Task>()};
        auto taskInitData{std::make_shared<data::SharedStruct>(environment)};
        auto taskRetData{std::make_shared<data::SharedStruct>(environment)};
        auto taskCompData{std::make_shared<data::SharedStruct>(environment)};
        auto subTask1{std::make_unique<SubTaskStub>("subTask1")};
        auto subTask2{std::make_unique<SubTaskStub>("subTask2")};
        auto subTask3{std::make_unique<SubTaskStub>("subTask3", taskRetData)};
        auto finalize{std::make_unique<SubTaskStub>("comp", taskCompData)};
        task->setData(taskInitData);
        task->addSubtask(std::move(subTask1));
        task->addSubtask(std::move(subTask2));
        task->addSubtask(std::move(subTask3));
        task->setCompletion(std::move(finalize));
        taskManager->queueTask(task);
        // note, for this test, no worker allocated

        WHEN("Polling once for completion") {
            tasks::ExpireTime expireTime = tasks::ExpireTime::now();
            bool didComplete = task->waitForCompletion(expireTime);
            auto data = task->getData();
            THEN("Task returns completed") {
                REQUIRE(didComplete);
            }
            THEN("Subtask1 was visited") {
                REQUIRE(taskInitData->hasKey("subTask1"));
            }
            THEN("Subtask2 was visited") {
                REQUIRE(taskInitData->hasKey("subTask2"));
            }
            THEN("Subtask3 was visited") {
                REQUIRE(taskInitData->hasKey("subTask3"));
            }
            THEN("Task completion received data") {
                REQUIRE(taskCompData->hasKey("_comp"));
                auto compStruct = taskCompData->get("_comp").castObject<data::SharedStruct>();
                REQUIRE(compStruct == taskRetData);
            }
            THEN("Task returned data") {
                REQUIRE(data == taskRetData);
            }
        }
    }

    GIVEN("Three sub-tasks returning early with completion function") {
        auto taskAnchor{taskManager->createTask()};
        auto task{taskAnchor.getObject<tasks::Task>()};
        auto taskInitData{std::make_shared<data::SharedStruct>(environment)};
        auto taskRetData2{std::make_shared<data::SharedStruct>(environment)};
        auto taskRetData3{std::make_shared<data::SharedStruct>(environment)};
        auto taskCompData{std::make_shared<data::SharedStruct>(environment)};
        auto subTask1{std::make_unique<SubTaskStub>("subTask1")};
        auto subTask2{std::make_unique<SubTaskStub>("subTask2", taskRetData2)};
        auto subTask3{std::make_unique<SubTaskStub>("subTask3", taskRetData3)};
        auto finalize{std::make_unique<SubTaskStub>("comp", taskCompData)};
        task->setData(taskInitData);
        task->addSubtask(std::move(subTask1));
        task->addSubtask(std::move(subTask2));
        task->addSubtask(std::move(subTask3));
        task->setCompletion(std::move(finalize));
        taskManager->queueTask(task);
        // note, for this test, no worker allocated

        WHEN("Polling once for completion") {
            tasks::ExpireTime expireTime = tasks::ExpireTime::now();
            bool didComplete = task->waitForCompletion(expireTime);
            auto data = task->getData();
            THEN("Task returns completed") {
                REQUIRE(didComplete);
            }
            THEN("Subtask1 was visited") {
                REQUIRE(taskInitData->hasKey("subTask1"));
            }
            THEN("Subtask2 was visited") {
                REQUIRE(taskInitData->hasKey("subTask2"));
            }
            THEN("Subtask3 was not visited") {
                REQUIRE_FALSE(taskInitData->hasKey("subTask3"));
            }
            THEN("Task completion received Subtask2 data") {
                REQUIRE(taskCompData->hasKey("_comp"));
                auto compStruct = taskCompData->get("_comp").castObject<data::SharedStruct>();
                REQUIRE(compStruct == taskRetData2);
            }
            THEN("Task returned Subtask2 data") {
                REQUIRE(data == taskRetData2);
            }
        }
    }

    GIVEN("An unqueued task") {
        auto taskAnchor{taskManager->createTask()};
        auto task{taskAnchor.getObject<tasks::Task>()};
        auto taskInitData{std::make_shared<data::SharedStruct>(environment)};
        task->setData(taskInitData);
        // note, for this test, do not queue
        // note, for this test, no worker allocated

        WHEN("Waiting for cancelled task") {
            task->cancelTask();
            auto t1 = std::chrono::system_clock::now();
            tasks::ExpireTime expireTime = tasks::ExpireTime::fromNowSecs(10);
            bool didComplete = task->waitForCompletion(expireTime);
            auto data = task->getData();
            auto tDelta = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now() - t1
            )
                              .count();
            THEN("Task did not block") {
                REQUIRE(tDelta < 9);
            }
            AND_THEN("Task returns not complete") {
                REQUIRE_FALSE(didComplete);
            }
        }
    }
}

SCENARIO("Deferred task management", "[tasks]") {
    data::Environment environment;
    std::shared_ptr<tasks::TaskManager> taskManager{
        std::make_shared<tasks::TaskManager>(environment)};
    tasks::FixedTaskThreadScope threadScope{
        std::make_shared<tasks::FixedTimerTaskThread>(environment, taskManager)};

    GIVEN("Three independent tasks") {
        auto taskAnchor1{taskManager->createTask()};
        auto taskAnchor2{taskManager->createTask()};
        auto taskAnchor3{taskManager->createTask()};
        auto task1{taskAnchor1.getObject<tasks::Task>()};
        auto task2{taskAnchor2.getObject<tasks::Task>()};
        auto task3{taskAnchor3.getObject<tasks::Task>()};
        auto taskInitData1{std::make_shared<data::SharedStruct>(environment)};
        auto taskInitData2{std::make_shared<data::SharedStruct>(environment)};
        auto taskInitData3{std::make_shared<data::SharedStruct>(environment)};
        auto taskRetData1{std::make_shared<data::SharedStruct>(environment)};
        auto taskRetData2{std::make_shared<data::SharedStruct>(environment)};
        auto taskRetData3{std::make_shared<data::SharedStruct>(environment)};
        auto finalize1{std::make_unique<SubTaskStub>("task1", taskRetData1)};
        auto finalize2{std::make_unique<SubTaskStub>("task2", taskRetData2)};
        auto finalize3{std::make_unique<SubTaskStub>("task3", taskRetData3)};
        task1->setData(taskInitData1);
        task2->setData(taskInitData2);
        task3->setData(taskInitData3);
        task1->setCompletion(std::move(finalize1));
        task2->setCompletion(std::move(finalize2));
        task3->setCompletion(std::move(finalize3));
        auto now = tasks::ExpireTime::now();
        auto task1Time = now + std::chrono::milliseconds(TIMER_GRANULARITY * 2);
        auto task2Time = now + std::chrono::milliseconds(TIMER_GRANULARITY * 4);
        auto task3Time = now + std::chrono::milliseconds(TIMER_GRANULARITY * 6);
        auto maxTime = now + std::chrono::milliseconds(TIMER_GRANULARITY * 8);
        task1->setStartTime(task1Time);
        task2->setStartTime(task2Time);
        task3->setStartTime(task3Time);
        taskManager->queueTask(task3);
        taskManager->queueTask(task1);
        taskManager->queueTask(task2);

        WHEN("Waiting for all three tasks completed") {
            bool didComplete2 = task2->waitForCompletion(maxTime);
            bool didComplete1 = task1->waitForCompletion(maxTime);
            bool didComplete3 = task3->waitForCompletion(maxTime);
            THEN("Tasks did complete") {
                REQUIRE(didComplete1);
                REQUIRE(didComplete2);
                REQUIRE(didComplete3);
                AND_THEN("Tasks completed in correct order") {
                    uint64_t thenAsMillis = now.asMilliseconds();
                    uint64_t task1Millis = taskRetData1->get("$task1").getInt();
                    uint64_t task2Millis = taskRetData2->get("$task2").getInt();
                    uint64_t task3Millis = taskRetData3->get("$task3").getInt();
                    REQUIRE(task1Millis > thenAsMillis);
                    REQUIRE(task2Millis > task1Millis);
                    REQUIRE(task3Millis > task2Millis);
                    AND_THEN("Tasks were delayed") {
                        REQUIRE((task1Millis - thenAsMillis) >= TIMER_GRANULARITY * 1);
                        REQUIRE((task2Millis - thenAsMillis) > TIMER_GRANULARITY * 3);
                        REQUIRE((task3Millis - thenAsMillis) > TIMER_GRANULARITY * 5);
                    }
                    AND_THEN("Tasks did not take too long") {
                        REQUIRE((task1Millis - thenAsMillis) < TIMER_GRANULARITY * 3);
                        REQUIRE((task2Millis - thenAsMillis) < TIMER_GRANULARITY * 5);
                        REQUIRE((task3Millis - thenAsMillis) < TIMER_GRANULARITY * 7);
                    }
                }
            }
        }
        WHEN("Tasks start times are modified") {
            task1->setStartTime(task3Time);
            task3->setStartTime(task1Time);
            AND_WHEN("Waiting for tasks to complete") {
                bool didComplete2 = task2->waitForCompletion(maxTime);
                bool didComplete1 = task1->waitForCompletion(maxTime);
                bool didComplete3 = task3->waitForCompletion(maxTime);
                THEN("Tasks did complete") {
                    REQUIRE(didComplete1);
                    REQUIRE(didComplete2);
                    REQUIRE(didComplete3);
                    AND_THEN("Tasks completed in correct order") {
                        uint64_t thenAsMillis = now.asMilliseconds();
                        uint64_t task1Millis = taskRetData1->get("$task1").getInt();
                        uint64_t task2Millis = taskRetData2->get("$task2").getInt();
                        uint64_t task3Millis = taskRetData3->get("$task3").getInt();
                        REQUIRE(task3Millis > thenAsMillis);
                        REQUIRE(task2Millis > task3Millis);
                        REQUIRE(task1Millis > task2Millis);
                    }
                }
            }
        }
        WHEN("A task is cancelled") {
            task2->cancelTask();
            AND_WHEN("Waiting for tasks to complete") {
                bool didComplete2 = task2->waitForCompletion(maxTime);
                bool didComplete1 = task1->waitForCompletion(maxTime);
                bool didComplete3 = task3->waitForCompletion(maxTime);
                THEN("Expected tasks did complete") {
                    REQUIRE(didComplete1);
                    REQUIRE_FALSE(didComplete2);
                    REQUIRE(didComplete3);
                    AND_THEN("Tasks completed in correct order") {
                        uint64_t thenAsMillis = now.asMilliseconds();
                        uint64_t task1Millis = taskRetData1->get("$task1").getInt();
                        uint64_t task3Millis = taskRetData3->get("$task3").getInt();
                        REQUIRE(task1Millis > thenAsMillis);
                        REQUIRE(task3Millis > task1Millis);
                    }
                }
            }
        }
    }
}

// NOLINTEND
