#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <optional>
#include <variant>

#include "scripting.hpp"

/* events */
/*
 * the events work with guards to correctly transition between the states
 * the events are partitioned to simplify the guard behavior
 */
struct eventInitialize {
}; /** Used to move from the iniitial state to NEW.  Probably can become Skip */
struct eventUpdate {}; /** Used by the states to indicate a change in the requests */
struct eventSkip {}; /** Used by some states to skip to the next happy-path state in the sequence */
struct eventScriptEventError {}; /** Used to indicate a script has completed with an error */
struct eventScriptEventOK {}; /** Used to indicate a script has completed with no error */

using Event = std::
    variant<eventInitialize, eventUpdate, eventSkip, eventScriptEventError, eventScriptEventOK>;

struct component_data;

constexpr int MAXERRORS = 3;

struct errorRate {
    errorRate() {
        clearErrors();
    }
    typedef std::array<std::chrono::steady_clock::time_point, MAXERRORS> history_t;
    using clock = std::chrono::steady_clock;

    void newError() {
        std::rotate(_history.begin(), _history.begin() + 1, _history.end()); // rotate to the left
        _history.back() = clock::now();
    }

    bool isBroken() {
        if(_history.front() == clock::time_point()) {
            return false;
        }
        auto age = _history.back() - _history.front();
        using namespace std::chrono_literals;
        return age < 1h;
    }

    constexpr void clearErrors() {
        _history.fill(clock::time_point());
    }

private:
    history_t _history;
};

struct state_data {
    state_data(
        std::optional<scriptRunner> installer = std::nullopt,
        std::optional<scriptRunner> starter = std::nullopt,
        std::optional<scriptRunner> runner = std::nullopt,
        std::optional<scriptRunner> stopper = std::nullopt)
        : installScript(std::move(installer)), startScript(std::move(starter)),
          runScript(std::move(runner)), shutdownScript(std::move(stopper)), start(false),
          restart(false), reinstall(false), stop(false){};
    bool start;
    bool restart;
    bool reinstall;
    bool stop;

    std::optional<scriptRunner> installScript;
    std::optional<scriptRunner> startScript;
    std::optional<scriptRunner> runScript;
    std::optional<scriptRunner> shutdownScript;

    void killAll() {
        if(installScript) {
            installScript->kill();
        }
        if(startScript) {
            startScript->kill();
        }
        if(runScript) {
            runScript->kill();
        }
        if(shutdownScript) {
            shutdownScript->kill();
        }
    }

    errorRate installErrors;
    errorRate startErrors;
    errorRate runErrors;
    errorRate stopErrors;
};

using state_data_v = std::variant<state_data>;

/* states */
struct state_base {
    virtual void operator()(component_data &, state_data &) = 0;
    virtual std::string_view getName() = 0;
};

struct Initial : state_base {
    void operator()(component_data &, state_data &);
    std::string_view getName() {
        return "Initial";
    }
};
struct New : state_base {
    void operator()(component_data &, state_data &);
    std::string_view getName() {
        return "New";
    }
};
struct Installing : state_base {
    void operator()(component_data &, state_data &);
    std::string_view getName() {
        return "Installing";
    }
};
struct Installed : state_base {
    void operator()(component_data &, state_data &);
    std::string_view getName() {
        return "Installed";
    }
};
struct Broken : state_base {
    void operator()(component_data &, state_data &);
    std::string_view getName() {
        return "Broken";
    }
};
struct Starting : state_base {
    void operator()(component_data &, state_data &);
    std::string_view getName() {
        return "Starting";
    }
};
struct Running : state_base {
    void operator()(component_data &, state_data &);
    std::string_view getName() {
        return "Running";
    }
};
struct Stopping : state_base {
    void operator()(component_data &, state_data &);
    std::string_view getName() {
        return "Stopping";
    }
};
struct Finished : state_base {
    void operator()(component_data &, state_data &);
    std::string_view getName() {
        return "Finished";
    }
};
struct StoppingWError : state_base {
    void operator()(component_data &, state_data &);
    std::string_view getName() {
        return "Stopping w/ Error";
    }
};
struct KillWStopError : state_base {
    void operator()(component_data &, state_data &);
    std::string_view getName() {
        return "Kill w/ Stop Error";
    }
};
struct KillWRunError : state_base {
    void operator()(component_data &, state_data &);
    std::string_view getName() {
        return "Kill w/ Run Error";
    }
};
struct Kill : state_base {
    void operator()(component_data &, state_data &);
    std::string_view getName() {
        return "Kill";
    }
};

using State = std::variant<
    Initial,
    New,
    Installing,
    Installed,
    Broken,
    Starting,
    Running,
    Stopping,
    Finished,
    StoppingWError,
    KillWStopError,
    KillWRunError,
    Kill>;

/* transitions */
struct Transitions {

    std::optional<State> operator()(Initial &, const eventInitialize &e, state_data &s) {
        std::cout << "Initial -> New" << std::endl;
        return New();
    }

    std::optional<State> operator()(New &, const eventUpdate &e, state_data &s) {
        if(s.start || s.restart || s.reinstall) {
            std::cout << "New ->";
            if(s.installScript) {
                std::cout << "Installing" << std::endl;
                return Installing();
            } else {
                std::cout << "Installed" << std::endl;
                return Installed();
            }
        } else {
            return {};
        }
    }

    std::optional<State> operator()(Installing &, const eventSkip &e, state_data &s) {
        std::cout << "Installing -> Installed (skip)" << std::endl;
        return Installed();
    }

    std::optional<State> operator()(Installing &, const eventScriptEventOK &e, state_data &s) {
        std::cout << "Installing -> Installed" << std::endl;
        return Installed();
    }

    std::optional<State> operator()(Installing &, const eventScriptEventError &e, state_data &s) {
        std::cout << "Installing -> ";
        s.installErrors.newError();
        if(s.installErrors.isBroken()) {
            std::cout << "Broken" << std::endl;
            return Broken();
        } else {
            std::cout << "Installing" << std::endl;
            return Installing();
        }
    }

    std::optional<State> operator()(Installed &, const eventUpdate &e, state_data &s) {
        std::cout << "Installed -> Starting" << std::endl;
        return Starting();
    }

    std::optional<State> operator()(Starting &, const eventUpdate &e, state_data &s) {
        std::cout << "Starting -> Running" << std::endl;
        return Running();
    }

    std::optional<State> operator()(Starting &, const eventSkip &e, state_data &s) {
        std::cout << "Starting -> Running" << std::endl;
        return Running();
    }

    std::optional<State> operator()(Running &, const eventUpdate &e, state_data &s) {
        std::cout << "Running -> Stopping" << std::endl;
        return Stopping();
    }

    std::optional<State> operator()(Stopping &, const eventSkip &e, state_data &s) {
        std::cout << "Stopping -> Finished (skip)" << std::endl;
        return Finished();
    }

    std::optional<State> operator()(Stopping &, const eventScriptEventOK &e, state_data &s) {
        std::cout << "Stopping -> KILL" << std::endl;
        return Kill();
    }

    std::optional<State> operator()(Stopping &, const eventScriptEventError &e, state_data &s) {
        std::cout << "Stopping -> KILL w/ Error" << std::endl;
        s.stopErrors.newError();
        return KillWStopError();
    }

    std::optional<State> operator()(Kill &, const eventSkip &e, state_data &s) {
        std::cout << "Kill -> Finished";
        return Finished();
    }

    std::optional<State> operator()(KillWStopError &, const eventSkip &e, state_data &s) {
        std::cout << "Kill w/ Stop Error ";
        if(s.stopErrors.isBroken()) {
            std::cout << "Broken" << std::endl;
            return Broken();
        } else {
            std::cout << "Finished" << std::endl;
            return Finished();
        }
    }

    std::optional<State> operator()(KillWRunError &, const eventSkip &e, state_data &s) {
        std::cout << "Kill w/ Run Error -> ";
        if(s.stopErrors.isBroken()) {
            std::cout << "Broken" << std::endl;
            return Broken();
        } else {
            std::cout << "Finished" << std::endl;
            return Finished();
        }
    }

    std::optional<State> operator()(Finished &, const eventUpdate &e, state_data &s) {
        if(s.restart || s.reinstall) {
            std::cout << "Finished -> Installed" << std::endl;
            return Installed();
        } else {
            return {};
        }
    }

    std::optional<State> operator()(StoppingWError &, const eventSkip &e, state_data &s) {
        std::cout << "Stopping w/ Error -> Kill w/ Run Error" << std::endl;
        return KillWRunError{};
    }

    std::optional<State> operator()(StoppingWError &, const eventScriptEventOK &e, state_data &s) {
        std::cout << "Stopping w/ Error -> Kill w/ Run Error" << std::endl;
        return KillWRunError{};
    }

    std::optional<State> operator()(
        StoppingWError &, const eventScriptEventError &e, state_data &s) {
        std::cout << "Stopping w/ Error -> Kill w/ Run Error" << std::endl;
        return KillWRunError{};
    }

    // Default do-nothing
    template<typename State_t, typename Event_t>
    std::optional<State> operator()(State_t &, const Event_t &, state_data &s) const {
        std::cout << "Unknown" << std::endl;
        return {};
    }
};

template<typename StateVariant, typename EventVariant, typename Transitions>
struct lifecycle {

    lifecycle(
        component_data &data,
        scriptRunner &installerRunner,
        scriptRunner &startupRunner,
        scriptRunner &runRunner,
        scriptRunner &shutdownRunner)
        : componentData(data),
          stateData(installerRunner, startupRunner, runRunner, shutdownRunner) {
        dispatch(eventInitialize{});
    };

    StateVariant m_curr_state;

    void dispatch(const EventVariant &Event) {
        std::optional<StateVariant> new_state =
            std::visit(Transitions{}, m_curr_state, Event, stateData);
        if(new_state) {
            m_curr_state = *std::move(new_state);
            std::visit(
                [this](auto &&newState) {
                    newState(componentData, std::get<state_data>(stateData));
                },
                m_curr_state);
        }
    }

    /* TODO: probably do not need runEvents */
    template<typename... Events>
    void runEvents(Events... e) {
        (dispatch(e), ...);
    }

    void scriptEvent(bool ok) {
        if(ok) {
            dispatch(eventScriptEventOK{});
        } else {
            dispatch(eventScriptEventError{});
        }
    }

    void setStop() {
        std::get<state_data>(stateData).stop = true;
        dispatch(eventUpdate{});
    }

    void setStart() {
        std::get<state_data>(stateData).start = true;
        dispatch(eventUpdate{});
    }

    void setRestart() {
        std::get<state_data>(stateData).restart = true;
        dispatch(eventUpdate{});
    }

    void setReinstall() {
        std::get<state_data>(stateData).reinstall = true;
        dispatch(eventUpdate{});
    }

private:
    component_data &componentData;
    state_data_v stateData;
};

struct component_data {
    component_data(
        std::string_view name,
        std::function<void(void)> skipSender,
        std::function<void(void)> updateSender)
        : _name(name), _skipper(skipSender), _updater(updateSender){};
    void skip() {
        _skipper();
    }
    void update() {
        _updater();
    }
    std::string_view getName() {
        return _name;
    }

private:
    std::string _name;
    std::function<void(void)> _skipper;
    std::function<void(void)> _updater;
};

struct component {
    component(
        std::string_view name,
        scriptRunner &installRunner,
        scriptRunner &startupRunner,
        scriptRunner &runRunner,
        scriptRunner &shutdwonRunner)
        : _fsm(theData, installRunner, startupRunner, runRunner, shutdownRunner),
          theData(
              name, [this]() { sendSkipEvent(); }, [this]() { sendUpdateEvent(); }){};

    void requestStart() {
        _fsm.setStart();
    }

    void requestStop() {
        _fsm.setStop();
    }

    void requestRestart() {
        _fsm.setRestart();
    }

    void requestReinstall() {
        _fsm.setReinstall();
    }

    void scriptEvent(bool ok) {
        _fsm.scriptEvent(ok);
    }

private:
    void sendSkipEvent() {
        _fsm.dispatch(eventSkip{});
    }
    void sendUpdateEvent() {
        _fsm.dispatch(eventUpdate{});
    }
    component_data theData;
    lifecycle<State, Event, Transitions> _fsm;
};
