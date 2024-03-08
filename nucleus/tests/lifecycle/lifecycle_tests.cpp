#include "test_ggroot.hpp"
#include "trompeloeil.hpp"
#include <catch2/catch_all.hpp>
#include <catch2/trompeloeil.hpp>
#include <lifecycle/lifecycle_fsm.hpp>
#include <lifecycle/scripting.hpp>
#include <type_traits>
#include <variant>

using Catch::Matchers::Equals;
namespace mock = trompeloeil;

// NOLINTBEGIN(cert-err58-cpp)

std::ostream &operator<<(std::ostream &os, event::Event e) {
    return std::visit(
        [&os](auto &&e) -> std::ostream & {
            return os << "event::" << std::remove_reference_t<decltype(e)>::name;
        },
        e);
}

std::ostream &operator<<(std::ostream &os, State s) {
    return std::visit(
        [&os](auto &&s) -> std::ostream & {
            return os << "state::" << std::remove_reference_t<decltype(s)>::name;
        },
        s);
}

class TestComponentListener : public ComponentListener {
public:
    using ComponentListener::ComponentListener;
    MAKE_MOCK2(alertStateChange, void(const State &, const State &), override);
    MAKE_MOCK2(alertStateUnchanged, void(const State &, const event::Event &), override);
    MAKE_MOCK0(skip, void(void), override);
    MAKE_MOCK0(update, void(void), override);
};

template<class MemberT, class VariantT>
static auto matchVariant() {
    return mock::make_matcher<VariantT>(
        [](const VariantT &request) { return std::holds_alternative<MemberT>(request); },
        [](std::ostream &os) { os << " Not matching " << MemberT{} << "\n"; });
}

template<class T>
static const auto matchState = matchVariant<T, State>;

template<class T>
static const auto matchEvent = matchVariant<T, event::Event>;

template<class StateT, class EventT, class Fn>
void requireNoTransition(TestComponentListener &listener, ComponentLifecycle &lifecycle, Fn &&fn) {
    lifecycle.clearFlags();
    {
        ALLOW_CALL(listener, update());
        lifecycle.overrideState(StateT{});
    }
    FORBID_CALL(listener, alertStateChange(mock::_, mock::_));
    FORBID_CALL(listener, skip());
    FORBID_CALL(listener, update());
    REQUIRE_CALL(listener, alertStateUnchanged(matchState<StateT>(), matchEvent<EventT>()))
        .TIMES(1);
    fn();
}

template<class StateT, class... MemFns>
void requireNoTransitionWithFlag(
    TestComponentListener &listener, ComponentLifecycle &lifecycle, MemFns &&...fns) {
    (requireNoTransition<StateT, event::Update>(
         listener,
         lifecycle,
         [&] {
             (lifecycle.*fns)();
             lifecycle.dispatch(event::Update{});
         }),
     ...);
}

template<class StateT, class... Events>
void requireNoTransitionOnEvent(
    TestComponentListener &listener, ComponentLifecycle &lifecycle, Events &&...events) {
    (requireNoTransition<StateT, Events>(
         listener, lifecycle, [&] { lifecycle.dispatch(std::forward<Events>(events)); }),
     ...);
}

template<class SourceT, class DesiredT, class Fn>
void requireTransition(TestComponentListener &listener, ComponentLifecycle &lifecycle, Fn &&fn) {
    lifecycle.clearFlags();
    {
        ALLOW_CALL(listener, update());
        ALLOW_CALL(listener, skip());
        lifecycle.overrideState(SourceT{});
    }
    FORBID_CALL(listener, alertStateUnchanged(mock::_, mock::_));
    REQUIRE_CALL(listener, alertStateChange(matchState<SourceT>(), matchState<DesiredT>()))
        .TIMES(1);
    fn();
}

template<class SourceT, class DesiredT, class... MemFns>
void requireTransitionWithFlag(
    TestComponentListener &listener, ComponentLifecycle &lifecycle, MemFns &&...fns) {
    (requireTransition<SourceT, DesiredT>(
         listener,
         lifecycle,
         [&] {
             (lifecycle.*fns)();
             lifecycle.dispatch(event::Update{});
         }),
     ...);
}

template<class SourceT, class DesiredT, class... Events>
void requireStateChangeOnEvent(
    TestComponentListener &listener, ComponentLifecycle &lifecycle, Events &&...events) {
    (requireTransition<SourceT, DesiredT>(
         listener, lifecycle, [&] { lifecycle.dispatch(std::forward<Events>(events)); }),
     ...);
}

SCENARIO("Basic Component Lifecycle", "[lifecycle]") {

    TestComponentListener listener{"test"};
    GIVEN("Initial Lifecycle, no scripts") {
        ComponentLifecycle lifecycle{listener, StateData{}};

        WHEN("In the Initial state") {
            AND_WHEN("Dispatching initialize event") {
                THEN("Lifecycle enters the New state") {
                    ALLOW_CALL(listener, update());
                    FORBID_CALL(listener, skip());
                    requireStateChangeOnEvent<Initial, New>(
                        listener, lifecycle, event::Initialize{});
                }
            }
            AND_WHEN("Dispatching all other events") {
                THEN("Lifecycle stays in the current state") {
                    requireNoTransitionOnEvent<Initial>(
                        listener,
                        lifecycle,
                        event::Skip{},
                        event::Update{},
                        event::ScriptError{},
                        event::ScriptOk{});

                    requireNoTransitionWithFlag<Initial>(
                        listener,
                        lifecycle,
                        &ComponentLifecycle::setReinstall,
                        &ComponentLifecycle::setStop,
                        &ComponentLifecycle::setRestart,
                        &ComponentLifecycle::setStart);
                }
            }
        }

        WHEN("In the New state") {
            AND_WHEN("Setting no flags and dispatching an event") {
                THEN("Lifecycle remains in New state") {
                    requireNoTransitionOnEvent<New>(
                        listener,
                        lifecycle,
                        event::Initialize{},
                        event::Skip{},
                        event::Update{},
                        event::ScriptError{},
                        event::ScriptOk{});
                }
            }

            AND_WHEN("Setting start||reinstall||restart and dispatching Update") {
                THEN("Lifecycle enters Installed state") {
                    requireTransitionWithFlag<New, Installed>(
                        listener,
                        lifecycle,
                        &ComponentLifecycle::setStart,
                        &ComponentLifecycle::setReinstall,
                        &ComponentLifecycle::setRestart);
                }
            }

            AND_WHEN("Setting stop and sending update") {
                THEN("Lifecycle remains in New state") {
                    requireNoTransitionWithFlag<New>(
                        listener, lifecycle, &ComponentLifecycle::setStop);
                }
            }
        }

        WHEN("In the Installing State") {
            AND_WHEN("scriptError is received") {
                AND_WHEN("errorRate is Ok") {
                    THEN("Installing state is restarted") {
                        requireStateChangeOnEvent<Installing, Installing>(
                            listener, lifecycle, event::ScriptError{});
                    }
                }

                AND_WHEN("errorRate is exceeded") {
                    requireStateChangeOnEvent<Installing, Installing>(
                        listener, lifecycle, event::ScriptError{}, event::ScriptError{});
                    THEN("Lifecycle transitions to Broken") {
                        requireStateChangeOnEvent<Installing, Broken>(
                            listener, lifecycle, event::ScriptError{});
                    }
                }
            }
        }

        WHEN("In the Broken state") {
            AND_WHEN("start||restart||reinstall is set on entry") {
                THEN("Update is triggered") {
                    REQUIRE_CALL(listener, update());
                    lifecycle.clearFlags();
                    lifecycle.setStart();
                    lifecycle.overrideState(Broken{});

                    REQUIRE_CALL(listener, update());
                    lifecycle.clearFlags();
                    lifecycle.setRestart();
                    lifecycle.overrideState(Broken{});

                    REQUIRE_CALL(listener, update());
                    lifecycle.clearFlags();
                    lifecycle.setReinstall();
                    lifecycle.overrideState(Broken{});
                }
            }

            AND_WHEN("stop is set on Update") {
                THEN("Lifecycle transitions to New state") {
                    requireTransitionWithFlag<Broken, New>(
                        listener, lifecycle, &ComponentLifecycle::setStop);
                }
            }
        }

        WHEN("In the Startup state") {
            AND_WHEN("Script Ok event is received") {
                THEN("Lifecycle transitions to Running") {
                    requireStateChangeOnEvent<Startup, Running>(
                        listener, lifecycle, event::ScriptOk{});
                }
            }
            AND_WHEN("Error event is received") {
                AND_WHEN("errorRate is Ok") {
                    THEN("Lifecycle state is set to Installed") {
                        requireStateChangeOnEvent<Startup, Installed>(
                            listener, lifecycle, event::ScriptError{});
                    }
                }

                AND_WHEN("errorRate is exceeded") {
                    requireStateChangeOnEvent<Startup, Installed>(
                        listener, lifecycle, event::ScriptError{}, event::ScriptError{});
                    THEN("Lifecycle transitions to Broken") {
                        requireStateChangeOnEvent<Startup, Broken>(
                            listener, lifecycle, event::ScriptError{});
                    }
                }
            }
        }
    }
}
// NOLINTEND(cert-err58-cpp)
