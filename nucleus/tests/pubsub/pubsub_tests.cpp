#include "data/globals.hpp"
#include "pubsub/local_topics.hpp"
#include "tasks/task_threads.hpp"
#include <catch2/catch_all.hpp>

// NOLINTBEGIN

class ListenerStub : public pubsub::AbstractCallback {
    data::Environment &_env;
    std::string _flagName;
    std::shared_ptr<data::StructModelBase> _returnData;

public:
    ListenerStub(
        data::Environment &env,
        const std::string_view &flagName,
        const std::shared_ptr<data::StructModelBase> &returnData
    )
        : _env(env), _flagName(flagName), _returnData{returnData} {
    }

    ListenerStub(data::Environment &env, const std::string_view &flagName)
        : _env(env), _flagName(flagName) {
    }

    data::ObjHandle operator()(
        data::ObjHandle taskHandle, data::StringOrd topicOrd, data::ObjHandle dataStruct
    ) override {
        std::shared_ptr<data::TrackingScope> scope =
            _env.handleTable.getObject<data::TrackingScope>(taskHandle);
        std::string topic = topicOrd ? _env.stringTable.getString(topicOrd) : "(anon)";
        std::shared_ptr<data::StructModelBase> data;
        if(dataStruct) {
            data = _env.handleTable.getObject<data::StructModelBase>(dataStruct);
            data->put(_flagName, topic);
        }
        if(_returnData) {
            _returnData->put(
                "_" + _flagName,
                data::StructElement{std::static_pointer_cast<data::ContainerModelBase>(data)}
            );
            return scope->anchor(_returnData).getHandle();
        } else {
            return {};
        }
    }
};

SCENARIO("PubSub Internal Behavior", "[pubsub]") {
    data::Global global;
    tasks::FixedTaskThreadScope threadScope{
        std::make_shared<tasks::FixedTaskThread>(global.environment, global.taskManager)};

    GIVEN("Some listeners") {
        data::StringOrd topic{global.environment.stringTable.getOrCreateOrd("topic")};
        data::StringOrd topic2{global.environment.stringTable.getOrCreateOrd("other-topic")};
        auto callRetData{std::make_shared<data::SharedStruct>(global.environment)};
        std::shared_ptr<pubsub::Listener> subs1{global.lpcTopics->subscribe(
            {}, std::make_unique<ListenerStub>(global.environment, "subs1")
        )};
        std::shared_ptr<pubsub::Listener> subs2{global.lpcTopics->subscribe(
            topic, std::make_unique<ListenerStub>(global.environment, "subs2", callRetData)
        )};
        std::shared_ptr<pubsub::Listener> subs3{global.lpcTopics->subscribe(
            topic, std::make_unique<ListenerStub>(global.environment, "subs3")
        )};
        std::shared_ptr<pubsub::Listener> subs4{global.lpcTopics->subscribe(
            topic2, std::make_unique<ListenerStub>(global.environment, "subs4", callRetData)
        )};
        WHEN("A query of topic listeners is made") {
            std::shared_ptr<pubsub::Listeners> listeners{global.lpcTopics->getListeners(topic)};
            THEN("The set of listeners is returned") {
                REQUIRE(static_cast<bool>(listeners));
                AND_WHEN("The set is queried") {
                    std::vector<std::shared_ptr<pubsub::Listener>> vec;
                    listeners->fillTopicListeners(vec);
                    THEN("The set is ordered correctly") {
                        REQUIRE_THAT(
                            vec,
                            Catch::Matchers::Equals(std::vector<std::shared_ptr<pubsub::Listener>>{
                                subs3, subs2})
                        );
                    }
                }
            }
        }
        WHEN("A query of other-topic listeners is made") {
            std::shared_ptr<pubsub::Listeners> listeners{global.lpcTopics->getListeners(topic2)};
            THEN("The set of listeners is returned") {
                REQUIRE(static_cast<bool>(listeners));
                AND_WHEN("The set is queried") {
                    std::vector<std::shared_ptr<pubsub::Listener>> vec;
                    listeners->fillTopicListeners(vec);
                    THEN("The set is ordered correctly") {
                        REQUIRE_THAT(
                            vec,
                            Catch::Matchers::Equals(std::vector<std::shared_ptr<pubsub::Listener>>{
                                subs4})
                        );
                    }
                }
            }
        }
        WHEN("A query of anon listeners is made") {
            std::shared_ptr<pubsub::Listeners> listeners{global.lpcTopics->getListeners({})};
            THEN("The set of anon listeners is returned") {
                REQUIRE(static_cast<bool>(listeners));
                AND_WHEN("The set is queried") {
                    std::vector<std::shared_ptr<pubsub::Listener>> vec;
                    listeners->fillTopicListeners(vec);
                    THEN("The set is correct") {
                        REQUIRE_THAT(
                            vec,
                            Catch::Matchers::UnorderedEquals(
                                std::vector<std::shared_ptr<pubsub::Listener>>{subs1}
                            )
                        );
                    }
                }
            }
        }
        WHEN("Performing an LPC call with topic") {
            tasks::ExpireTime expireTime = tasks::ExpireTime::now();
            auto callArgData{std::make_shared<data::SharedStruct>(global.environment)};
            auto newTaskAnchor{global.taskManager->createTask()};
            auto newTaskObj{newTaskAnchor.getObject<tasks::Task>()};
            global.lpcTopics->initializePubSubCall(
                newTaskObj, subs1, topic, callArgData, {}, tasks::ExpireTime::fromNowSecs(10)
            );
            global.taskManager->queueTask(newTaskObj);
            bool didComplete = newTaskObj->waitForCompletion(expireTime);
            auto returnedData = newTaskObj->getData();
            THEN("LPC completed") {
                REQUIRE(didComplete);
            }
            THEN("Topic listeners were visited") {
                // inserted first... is this correct? maybe doesn't matter
                REQUIRE(callArgData->hasKey("subs1"));
                REQUIRE(callArgData->get("subs1").getString() == "(anon)");
                // last-in-first-out
                REQUIRE(callArgData->hasKey("subs3"));
                REQUIRE(callArgData->get("subs3").getString() == "topic");
                REQUIRE(callArgData->hasKey("subs2"));
                REQUIRE(callArgData->get("subs2").getString() == "topic");
                // never added
                REQUIRE_FALSE(callArgData->hasKey("subs4"));
            }
            THEN("Expected structure was returned") {
                REQUIRE(static_cast<bool>(returnedData));
                REQUIRE(returnedData->hasKey("_subs2"));
                auto paramData = returnedData->get("_subs2").castObject<data::SharedStruct>();
                REQUIRE(paramData == callArgData);
            }
        }
        WHEN("Performing an Anon LPC call") {
            tasks::ExpireTime expireTime = tasks::ExpireTime::now();
            auto callArgData{std::make_shared<data::SharedStruct>(global.environment)};
            auto newTaskAnchor{global.taskManager->createTask()};
            auto newTaskObj{newTaskAnchor.getObject<tasks::Task>()};
            global.lpcTopics->initializePubSubCall(
                newTaskObj, subs1, {}, callArgData, {}, tasks::ExpireTime::fromNowSecs(10)
            );
            global.taskManager->queueTask(newTaskObj);
            bool didComplete = newTaskObj->waitForCompletion(expireTime);
            auto returnedData = newTaskObj->getData();
            THEN("Topic listeners were visited") {
                REQUIRE(callArgData->hasKey("subs1"));
                REQUIRE(callArgData->get("subs1").getString() == "(anon)");
                REQUIRE_FALSE(callArgData->hasKey("subs2"));
                REQUIRE_FALSE(callArgData->hasKey("subs3"));
                REQUIRE_FALSE(callArgData->hasKey("subs4"));
            }
            THEN("No value was returned") {
                REQUIRE_FALSE(static_cast<bool>(returnedData));
            }
        }
    }
}

// NOLINTEND
