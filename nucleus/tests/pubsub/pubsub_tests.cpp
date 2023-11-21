#include "pubsub/local_topics.hpp"
#include "scope/context_full.hpp"
#include "tasks/task_threads.hpp"
#include <catch2/catch_all.hpp>

// NOLINTBEGIN

using namespace std::literals;

class ListenerStub : public pubsub::AbstractCallback {
    std::shared_ptr<scope::Context> _context;
    std::string _flagName;
    std::shared_ptr<data::StructModelBase> _returnData;

public:
    ListenerStub(
        const std::shared_ptr<scope::Context> &context,
        const std::string_view &flagName,
        const std::shared_ptr<data::StructModelBase> &returnData)
        : _context(context), _flagName(flagName), _returnData{returnData} {
    }

    ListenerStub(const std::shared_ptr<scope::Context> &context, const std::string_view &flagName)
        : _context(context), _flagName(flagName) {
    }

    data::ObjHandle operator()(
        data::ObjHandle taskHandle, data::Symbol topicOrd, data::ObjHandle dataStruct) override {
        auto scope = taskHandle.toObject<tasks::Task>();
        auto topic = topicOrd.toStringOr("(anon)"s);
        std::shared_ptr<data::StructModelBase> data;
        if(dataStruct) {
            data = dataStruct.toObject<data::StructModelBase>();
            data->put(_flagName, topic);
        }
        if(_returnData) {
            _returnData->put(
                "_" + _flagName,
                data::StructElement{std::static_pointer_cast<data::ContainerModelBase>(data)});
            return scope::NucleusCallScopeContext::handle(_returnData);
        } else {
            return {};
        }
    }
};

SCENARIO("PubSub Internal Behavior", "[pubsub]") {
    scope::LocalizedContext forTesting{scope::Context::create()};
    auto context = forTesting.context()->context();

    GIVEN("Some listeners") {
        data::Symbol topic{context->intern("topic")};
        data::Symbol topic2{context->intern("other-topic")};
        auto callRetData{std::make_shared<data::SharedStruct>(context)};
        std::shared_ptr<pubsub::Listener> subs1{context->lpcTopics().subscribe(
            {}, std::make_unique<ListenerStub>(context, "subs1"), {})};
        std::shared_ptr<pubsub::Listener> subs2{context->lpcTopics().subscribe(
            topic, std::make_unique<ListenerStub>(context, "subs2", callRetData), {})};
        std::shared_ptr<pubsub::Listener> subs3{context->lpcTopics().subscribe(
            topic, std::make_unique<ListenerStub>(context, "subs3"), {})};
        std::shared_ptr<pubsub::Listener> subs4{context->lpcTopics().subscribe(
            topic2, std::make_unique<ListenerStub>(context, "subs4", callRetData), {})};
        WHEN("A query of topic listeners is made") {
            std::shared_ptr<pubsub::Listeners> listeners{context->lpcTopics().getListeners(topic)};
            THEN("The set of listeners is returned") {
                REQUIRE(static_cast<bool>(listeners));
                AND_WHEN("The set is queried") {
                    std::vector<std::shared_ptr<pubsub::Listener>> vec;
                    listeners->fillTopicListeners(vec);
                    THEN("The set is ordered correctly") {
                        REQUIRE_THAT(
                            vec,
                            Catch::Matchers::Equals(
                                std::vector<std::shared_ptr<pubsub::Listener>>{subs3, subs2}));
                    }
                }
            }
        }
        WHEN("A query of other-topic listeners is made") {
            std::shared_ptr<pubsub::Listeners> listeners{context->lpcTopics().getListeners(topic2)};
            THEN("The set of listeners is returned") {
                REQUIRE(static_cast<bool>(listeners));
                AND_WHEN("The set is queried") {
                    std::vector<std::shared_ptr<pubsub::Listener>> vec;
                    listeners->fillTopicListeners(vec);
                    THEN("The set is ordered correctly") {
                        REQUIRE_THAT(
                            vec,
                            Catch::Matchers::Equals(
                                std::vector<std::shared_ptr<pubsub::Listener>>{subs4}));
                    }
                }
            }
        }
        WHEN("A query of anon listeners is made") {
            std::shared_ptr<pubsub::Listeners> listeners{context->lpcTopics().getListeners({})};
            THEN("The set of anon listeners is returned") {
                REQUIRE(static_cast<bool>(listeners));
                AND_WHEN("The set is queried") {
                    std::vector<std::shared_ptr<pubsub::Listener>> vec;
                    listeners->fillTopicListeners(vec);
                    THEN("The set is correct") {
                        REQUIRE_THAT(
                            vec,
                            Catch::Matchers::UnorderedEquals(
                                std::vector<std::shared_ptr<pubsub::Listener>>{subs1}));
                    }
                }
            }
        }
        WHEN("Performing an LPC call with topic") {
            tasks::ExpireTime expireTime = tasks::ExpireTime::now();
            auto callArgData{std::make_shared<data::SharedStruct>(context)};
            auto newTask{std::make_shared<tasks::Task>(context)};
            context->lpcTopics().initializePubSubCall(
                newTask, subs1, topic, callArgData, {}, tasks::ExpireTime::fromNow(10s));
            context->taskManager().queueTask(newTask);
            bool didComplete = newTask->waitForCompletion(expireTime);
            auto returnedData = newTask->getData();
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
            auto callArgData{std::make_shared<data::SharedStruct>(context)};
            auto newTask{std::make_shared<tasks::Task>(context)};
            context->lpcTopics().initializePubSubCall(
                newTask, subs1, {}, callArgData, {}, tasks::ExpireTime::fromNow(10s));
            context->taskManager().queueTask(newTask);
            bool didComplete = newTask->waitForCompletion(expireTime);
            auto returnedData = newTask->getData();
            THEN("LPC completed") {
                REQUIRE(didComplete);
            }
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
