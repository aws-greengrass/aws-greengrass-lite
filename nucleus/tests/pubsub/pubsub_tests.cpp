#include "pubsub/local_topics.hpp"
#include "scope/context_full.hpp"
#include "tasks/task_callbacks.hpp"
#include <catch2/catch_all.hpp>

// NOLINTBEGIN

using namespace std::literals;

class ListenerStub : public tasks::Callback {
    std::string _flagName;
    std::shared_ptr<pubsub::Promise> _promise;
    bool _autoComplete;

public:
    ListenerStub(
        const scope::UsingContext &context,
        const std::string_view &flagName,
        const std::shared_ptr<pubsub::Promise> &promise,
        bool autoComplete)
        : tasks::Callback(context), _flagName(flagName), _promise{promise},
          _autoComplete{autoComplete} {
    }

    ListenerStub(const scope::UsingContext &context, const std::string_view &flagName)
        : tasks::Callback(context), _flagName(flagName) {
    }

    std::shared_ptr<pubsub::Future> invokeTopicCallback(
        const data::Symbol &topicSymbol,
        const std::shared_ptr<data::ContainerModelBase> &data) override {

        auto topic = topicSymbol.toStringOr("(anon)"s);
        if(data) {
            auto dataStruct = data->ref<data::StructModelBase>();
            dataStruct->put(_flagName, topic);
        }
        if(_promise) {
            if(_autoComplete) {
                _promise->setValue(data);
            }
            return _promise->getFuture();
        } else {
            return {};
        }
    }

    void invokeAsyncCallback() override {

        //        auto topic = topicSymbol.toStringOr("(anon)"s);
        //        if(data) {
        //            data->put(_flagName, topic);
        //        }
        //        if(_returnData) {
        //            _returnData->put(
        //                "_" + _flagName,
        //                data::StructElement{std::static_pointer_cast<data::ContainerModelBase>(data)});
        //        }
        //        return _returnData;
    }

    void invokeFutureCallback(const std::shared_ptr<pubsub::Future> &future) override {

        //        auto topic = topicSymbol.toStringOr("(anon)"s);
        //        if(data) {
        //            data->put(_flagName, topic);
        //        }
        //        if(_returnData) {
        //            _returnData->put(
        //                "_" + _flagName,
        //                data::StructElement{std::static_pointer_cast<data::ContainerModelBase>(data)});
        //        }
        //        return _returnData;
    }
};

SCENARIO("PubSub Internal Behavior", "[pubsub]") {
    scope::LocalizedContext forTesting{};
    auto context = forTesting.context()->context();

    GIVEN("Some listeners") {
        data::Symbol topic{context->intern("topic")};
        data::Symbol topic2{context->intern("other-topic")};
        std::shared_ptr<pubsub::Listener> subs1{
            context->lpcTopics().subscribe({}, std::make_shared<ListenerStub>(context, "subs1"))};
        auto promise2{std::make_shared<pubsub::Promise>(context)};
        std::shared_ptr<pubsub::Listener> subs2{context->lpcTopics().subscribe(
            topic, std::make_shared<ListenerStub>(context, "subs2", promise2, true))};
        std::shared_ptr<pubsub::Listener> subs3{context->lpcTopics().subscribe(
            topic, std::make_shared<ListenerStub>(context, "subs3"))};
        auto promise4{std::make_shared<pubsub::Promise>(context)};
        std::shared_ptr<pubsub::Listener> subs4{context->lpcTopics().subscribe(
            topic2, std::make_shared<ListenerStub>(context, "subs4", promise4, false))};
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
        WHEN("Performing an LPC callFirst with topic") {
            auto callArgData{std::make_shared<data::SharedStruct>(context)};
            auto future = context->lpcTopics().callFirst(topic, callArgData);
            THEN("Future was returned") {
                REQUIRE(future);
                AND_THEN("Promise is immediately valid") {
                    REQUIRE(future->isValid());
                }
                AND_THEN("Value is valid") {
                    auto futureVal = future->getValue();
                    REQUIRE(futureVal == callArgData);
                }
            }
        }
        WHEN("Performing an LPC callFirst with deferred promise") {
            auto callArgData{std::make_shared<data::SharedStruct>(context)};
            auto future = context->lpcTopics().callFirst(topic2, callArgData);
            THEN("Future was returned") {
                REQUIRE(future);
                AND_THEN("Promise is not yet valid") {
                    REQUIRE_FALSE(future->isValid());
                }
                promise4->setValue(callArgData);
                AND_WHEN("Waiting for promise") {
                    bool isValid = future->waitUntil(tasks::ExpireTime::fromNow(1s));
                    THEN("Wait succeeded") {
                        REQUIRE(isValid);
                    }
                    THEN("Future is valid") {
                        REQUIRE(future->isValid());
                    }
                    THEN("Value is valid") {
                        auto futureVal = future->getValue();
                        REQUIRE(futureVal == callArgData);
                    }
                }
            }
        }
    }
}

// NOLINTEND
