#include "scope/context_full.hpp"
#include "tasks/task_callbacks.hpp"
#include <catch2/catch_all.hpp>
#include <cpp_api.hpp>
#include <util.hpp>

// NOLINTBEGIN

template<typename Field>
size_t fieldAlign(size_t off) {
    size_t align = alignof(Field);
    return (off + align - 1) & ~(align - 1);
}

template<typename E, typename A>
A &field(A &field) {
    INFO("Field type mismatch");
    CHECK(std::is_same_v<E, A>);
    return field;
}

template<typename A>
size_t fieldSeq(uintptr_t base, size_t off, A &a) {
    uintptr_t fieldOff = reinterpret_cast<uintptr_t>(static_cast<void *>(&a)) - base;
    if(fieldOff < off) {
        INFO("Field moved before expected");
        CHECK(fieldOff == off);
    }
    if(fieldOff > off) {
        INFO("Field moved after expected");
        CHECK(fieldOff == off);
    }
    return off + sizeof(a);
}

template<typename A, typename B, typename... Rest>
size_t fieldSeq(uintptr_t base, size_t off, A &a, B &b, Rest &...rest) {
    size_t endOfA = fieldSeq(base, off, a);
    size_t alignedB = fieldAlign<B>(endOfA);
    return fieldSeq(base, alignedB, b, rest...);
}

/**
 * If this test breaks, it means one of the following:
 * 1/ Fields have been removed from structure (likely breaks backwards compatibility)
 * 2/ Fields have been reordered (breaks backwards compatibility)
 * 3/ New fields have been added to structure, but test not updated
 * 4/ Field sizes have changed
 */
template<typename Ref, typename... Rest>
void assertStructureUnchanged(Ref &ref, Rest &...rest) {
    size_t refSize = sizeof(Ref);
    uintptr_t structOff = reinterpret_cast<uintptr_t>(static_cast<void *>(&ref));
    size_t actSize = fieldSeq(structOff, 0, rest...);
    INFO("Expected structure size changed - add new fields to test");
    REQUIRE(refSize == actSize);
}

// Non-behavioral
TEST_CASE("Verify callback structure contracts", "[callable]") {
    SECTION("ggapiTopicCallbackData does not break backward compatibility") {
        ggapiTopicCallbackData d;
        INFO("ggapiTopicCallbackData");
        // Note, any new fields must be added to end
        // No fields may be removed, types cannot be changed
        assertStructureUnchanged(
            d,
            field<ggapiObjHandle>(d.taskHandle),
            field<ggapiSymbol>(d.topicSymbol),
            field<ggapiObjHandle>(d.dataStruct),
            field<ggapiObjHandle>(d.retDataStruct));
    }
    SECTION("ggapiLifecycleCallbackData does not break backward compatibility") {
        ggapiLifecycleCallbackData d;
        INFO("ggapiLifecycleCallbackData");
        // Note, any new fields must be added to end
        // No fields may be removed, types cannot be changed
        assertStructureUnchanged(
            d,
            field<ggapiObjHandle>(d.moduleHandle),
            field<ggapiSymbol>(d.phaseSymbol),
            field<ggapiObjHandle>(d.dataStruct),
            field<uint32_t>(d.retWasHandled));
    }
    SECTION("ggapiTaskCallbackData does not break backward compatibility") {
        ggapiTaskCallbackData d;
        INFO("ggapiTaskCallbackData");
        // Note, any new fields must be added to end
        // No fields may be removed, types cannot be changed
        assertStructureUnchanged(d, field<ggapiObjHandle>(d.dataStruct));
    }
    SECTION("ggapiChannelListenCallbackData does not break backward compatibility") {
        ggapiChannelListenCallbackData d;
        INFO("ggapiChannelListenCallbackData");
        // Note, any new fields must be added to end
        // No fields may be removed, types cannot be changed
        assertStructureUnchanged(d, field<ggapiObjHandle>(d.dataStruct));
    }
    SECTION("ggapiChannelCloseCallbackData does not break backward compatibility") {
        ggapiChannelCloseCallbackData d;
        INFO("ggapiChannelCloseCallbackData");
        // Note, currently _dummy field is not used, it just ensures a structure size of 1
        // this can be replaced with a 16-bit or 32-bit field without breaking compatibility
        assertStructureUnchanged(d, field<uint8_t>(d._dummy));
    }
}

// Behavioral
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
