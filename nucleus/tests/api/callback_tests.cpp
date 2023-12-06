#include "scope/context_full.hpp"
#include "tasks/task_callbacks.hpp"
#include <catch2/catch_all.hpp>
#include <cpp_api.hpp>
#include <util.hpp>

// NOLINTBEGIN

SCENARIO("callable", "[callable]") {
    scope::LocalizedContext forTesting{scope::Context::create()};
    auto context = forTesting.context()->context();

    GIVEN("A callback function") {
        struct Test {
            int counter = 5;
            ggapi::Struct myCallback(ggapi::Task, ggapi::Symbol, ggapi::Struct data) {
                counter++;
                return data;
            }

            ggapi::Struct moreComplexCallback(
                const std::string &stuff,
                int moreStuff,
                const ggapi::Task &task,
                ggapi::StringOrd topic,
                ggapi::Struct data) {

                auto res = ggapi::Struct::create();
                res.put("stuff", stuff);
                res.put("moreStuff", moreStuff);
                res.put("task", task); // passing in a handle
                res.put("topic", topic); // passing in a symbol
                res.put("data", data);
                return res;
            }
        };
        Test test;
        WHEN("Creating a callback as a lambda") {
            ggapi::TopicCallbackLambda lambda = [&test](auto task, auto topic, auto data) {
                return test.myCallback(task, topic, data);
            };
            auto obj = ggapi::TopicCallback::of(lambda);
            THEN("A callback handle is returned") {
                REQUIRE(obj.getHandleId() != 0);
            }
            AND_WHEN("Calling the callback") {
                auto callback = context->objFromInt<tasks::Callback>(obj.getHandleId());
                auto task = std::make_shared<tasks::Task>(context);
                auto topic = context->intern("test");
                auto data = std::make_shared<data::SharedStruct>(context);
                auto res = callback->invokeTopicCallback(task, topic, data);
                THEN("Return value is as expected") {
                    REQUIRE(res == data);
                }
                THEN("Callback changed state") {
                    REQUIRE(test.counter == 6);
                }
            }
        }
        WHEN("Using the callback in a stack scope safe way") {
            auto obj = ggapi::TopicCallback::of(&Test::myCallback, &test);
            THEN("A callback handle is returned") {
                REQUIRE(obj.getHandleId() != 0);
            }
            AND_WHEN("Calling the callback") {
                auto callback = context->objFromInt<tasks::Callback>(obj.getHandleId());
                auto task = std::make_shared<tasks::Task>(context);
                auto topic = context->intern("test");
                auto data = std::make_shared<data::SharedStruct>(context);
                auto res = callback->invokeTopicCallback(task, topic, data);
                THEN("Return value is as expected") {
                    REQUIRE(res == data);
                }
                THEN("Callback changed state") {
                    REQUIRE(test.counter == 6);
                }
            }
        }

        WHEN("Using ability to capture by-value arguments in a stack scope safe way") {
            auto obj =
                ggapi::TopicCallback::of(&Test::moreComplexCallback, &test, std::string{"foo"}, 5);
            THEN("A callback handle is returned") {
                REQUIRE(obj.getHandleId() != 0);
            }
            AND_WHEN("Calling the callback") {
                auto callback = context->objFromInt<tasks::Callback>(obj.getHandleId());
                auto task = std::make_shared<tasks::Task>(context);
                auto topic = context->intern("test");
                auto data = std::make_shared<data::SharedStruct>(context);
                auto res = callback->invokeTopicCallback(task, topic, data);
                THEN("Return value is as expected") {
                    REQUIRE(res->get("stuff").getString() == std::string("foo"));
                    REQUIRE(res->get("moreStuff").getInt() == 5);
                    REQUIRE(res->get("topic").getString() == std::string("test"));
                    REQUIRE(res->get("data").getObject() == data);
                }
            }
        }
    }
}

// NOLINTEND
