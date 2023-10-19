#include <catch2/catch_all.hpp>
#include "data/shared_struct.hpp"
#include "tasks/task.hpp"

// NOLINTBEGIN

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

// NOLINTEND
