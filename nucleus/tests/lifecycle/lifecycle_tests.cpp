#include "test_ggroot.hpp"
#include "trompeloeil.hpp"
#include <catch2/catch_all.hpp>
#include <catch2/trompeloeil.hpp>
#include <lifecycle/lifecycle_fsm.hpp>
#include <lifecycle/scripting.hpp>
#include <variant>

using Catch::Matchers::Equals;
namespace mock = trompeloeil;

class TestComponentListener : public ComponentListener {
public:
    explicit TestComponentListener(std::string name) noexcept : ComponentListener(std::move(name)) {
    }
    MAKE_MOCK2(alertStateChange, void(const State &, const State &), override);
    MAKE_MOCK2(alertStateUnchanged, void(const State &, const event::Event &), override);
    MAKE_MOCK0(skip, void(void), override);
    MAKE_MOCK0(update, void(void), override);
};

template<class MemberT, class VariantT>
static auto variantMatcher() {
    return mock::make_matcher<VariantT>(
        [](const VariantT &request) { return std::holds_alternative<MemberT>(request); },
        [](std::ostream &os) { os << " Not matching " << MemberT::name << "\n"; });
}

template<class T>
static const auto stateMatcher = variantMatcher<T, State>;

template<class T>
static const auto eventMatcher = variantMatcher<T, event::Event>;

template<class StateT>
void checkStateUnchanged(TestComponentListener &listener, ComponentLifecycle &lifecycle) {
}

template<class StateT, class Event, class... Events>
void checkStateUnchanged(TestComponentListener &listener, ComponentLifecycle &lifecycle) {
    REQUIRE_CALL(listener, alertStateUnchanged(stateMatcher<StateT>(), eventMatcher<Event>()))
        .TIMES(1);
    lifecycle.dispatch(Event{});
    checkStateUnchanged<StateT, Events...>(listener, lifecycle);
}

SCENARIO("Basic Component Lifecycle", "[.lifecycle]") {

    TestComponentListener listener{"test"};
    GIVEN("Initial FSM, no scripts") {
        ComponentLifecycle lifecycle{listener, StateData{}};

        WHEN("In initial state") {
            WHEN("Dispatching initialize event") {
                THEN("FSM enters the New state") {
                    FORBID_CALL(listener, alertStateUnchanged(mock::_, mock::_));
                    REQUIRE_CALL(
                        listener, alertStateChange(stateMatcher<Initial>(), stateMatcher<New>()))
                        .TIMES(1);
                    REQUIRE_CALL(listener, update()).TIMES(1);
                    FORBID_CALL(listener, skip());
                    lifecycle.dispatch(event::Initialize{});
                }
            }
            WHEN("Dispatching all other events") {
                THEN("FSM stays in the current state") {
                    FORBID_CALL(listener, alertStateChange(mock::_, mock::_));
                    FORBID_CALL(listener, skip());
                    FORBID_CALL(listener, update());
                    checkStateUnchanged<
                        Initial,
                        event::Skip,
                        event::Update,
                        event::ScriptError,
                        event::ScriptOk>(listener, lifecycle);
                }
            }
        }

        WHEN("In New state") {
            ALLOW_CALL(listener, update());
            lifecycle.overrideState(New{});
            WHEN("Triggering start||reinstall||restart and sending update") {
                auto entersInstallStateWhen = [&](auto &&action) {
                    THEN("Lifecycle enters Installed state") {
                        REQUIRE_CALL(
                            listener,
                            alertStateChange(stateMatcher<New>(), stateMatcher<Installed>()))
                            .TIMES(1);
                        REQUIRE_CALL(listener, update()).TIMES(1);
                        FORBID_CALL(listener, skip());
                        FORBID_CALL(listener, alertStateUnchanged(mock::_, mock::_));
                        action();
                    }
                };

                entersInstallStateWhen([&] { lifecycle.setReinstall(); });
                entersInstallStateWhen([&] { lifecycle.setStart(); });
                entersInstallStateWhen([&] { lifecycle.setRestart(); });
            }

            WHEN("Triggering stop and sending update") {
                THEN("Lifecycle state does not change") {
                    FORBID_CALL(
                        listener, alertStateChange(stateMatcher<New>(), stateMatcher<Installed>()));
                    FORBID_CALL(listener, update());
                    FORBID_CALL(listener, skip());
                    REQUIRE_CALL(
                        listener,
                        alertStateUnchanged(stateMatcher<New>(), eventMatcher<event::Update>()))
                        .TIMES(1);
                    lifecycle.setStop();
                }
            }
        }
    }
}
