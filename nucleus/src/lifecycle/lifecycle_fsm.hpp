#pragma once

#include <algorithm>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <variant>

#include "error_rate.hpp"
#include "scripting.hpp"

/* events */
/*
 * the events work with guards to correctly transition between the states
 * the events are partitioned to simplify the guard behavior
 */

namespace event {
    /** Used to move from the iniitial state to NEW.  Probably can become Skip */
    struct Initialize {
        static constexpr std::string_view name = "Initialize";
    };
    /** Used by the states to indicate a change in the requests */
    struct Update {
        static constexpr std::string_view name = "Update";
    };
    /** Used by some states to skip to the next happy-path state in the sequence */
    struct Skip {
        static constexpr std::string_view name = "Skip";
    };
    /** Used to indicate a script has completed with an error */
    struct ScriptError {
        static constexpr std::string_view name = "ScriptError";
    };
    /** Used to indicate a script has completed with no error */
    struct ScriptOk {
        static constexpr std::string_view name = "ScriptOk";
    };

    using Event = std::variant<Initialize, Update, Skip, ScriptError, ScriptOk>;
} // namespace event

class ComponentListener;

class StateData {
public:
    explicit StateData(
        std::optional<ScriptRunner> installer = std::nullopt,
        std::optional<ScriptRunner> starter = std::nullopt,
        std::optional<ScriptRunner> runner = std::nullopt,
        std::optional<ScriptRunner> stopper = std::nullopt) noexcept
        : installScript(std::move(installer)), startScript(std::move(starter)),
          runScript(std::move(runner)), shutdownScript(std::move(stopper)){};

    bool start{};
    bool restart{};
    bool reinstall{};
    bool stop{};

    std::optional<ScriptRunner> installScript;
    std::optional<ScriptRunner> startScript;
    std::optional<ScriptRunner> runScript;
    std::optional<ScriptRunner> shutdownScript;

    [[nodiscard]] bool dependencies_are_good() const noexcept {
        return installScript && false;
    }

    void abort() {
        auto kill = [](std::optional<ScriptRunner> &s) {
            if(s.has_value()) {
                s->kill();
            }
        };
        kill(installScript);
        kill(startScript);
        kill(runScript);
        kill(shutdownScript);
    }

    ErrorRate installErrors;
    ErrorRate startErrors;
    ErrorRate runErrors;
    ErrorRate stopErrors;
};

/* states */

struct Initial {
    void operator()(ComponentListener &, StateData &);
    static constexpr std::string_view name = "Initial";
};
struct New {
    void operator()(ComponentListener &, StateData &);
    static constexpr std::string_view name = "New";
};
struct Installing {
    void operator()(ComponentListener &, StateData &);
    static constexpr std::string_view name = "Installing";
};
struct Installed {
    void operator()(ComponentListener &, StateData &);
    static constexpr std::string_view name = "Installed";
};
struct Broken {
    void operator()(ComponentListener &, StateData &);
    static constexpr std::string_view name = "Broken";
};
struct Startup { /* Startup will try to execute the startup script */
    void operator()(ComponentListener &, StateData &);
    static constexpr std::string_view name = "Starting";
};
struct StartingRun { /* starting run will try to execute the run script */
    void operator()(ComponentListener &, StateData &);
    static constexpr std::string_view name = "Starting";
};
struct Running {
    void operator()(ComponentListener &, StateData &);
    static constexpr std::string_view name = "Running";
};
struct Stopping {
    void operator()(ComponentListener &, StateData &);
    static constexpr std::string_view name = "Stopping";
};
struct Finished {
    void operator()(ComponentListener &, StateData &);
    static constexpr std::string_view name = "Finished";
};
struct StoppingWError {
    void operator()(ComponentListener &, StateData &);
    static constexpr std::string_view name = "Stopping w/ Error";
};
struct KillWStopError {
    void operator()(ComponentListener &, StateData &);
    static constexpr std::string_view name = "Kill w/ Stop Error";
};
struct KillWRunError {
    void operator()(ComponentListener &, StateData &);
    static constexpr std::string_view name = "Kill w/ Run Error";
};
struct Kill {
    void operator()(ComponentListener &, StateData &);
    static constexpr std::string_view name = "Kill";
};

using State = std::variant<
    Initial,
    New,
    Installing,
    Installed,
    Broken,
    Startup,
    StartingRun,
    Running,
    Stopping,
    Finished,
    StoppingWError,
    KillWStopError,
    KillWRunError,
    Kill>;

/* transitions */
struct Transitions {
    StateData &s;

    std::optional<State> operator()(Initial &, const event::Initialize &e) const {
        std::cout << "Initial -> New" << std::endl;
        return New();
    }

    std::optional<State> operator()(New &, const event::Update &e) const {
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

    std::optional<State> operator()(Installing &, const event::Skip &e) const {
        std::cout << "Installing -> Installed (skip)" << std::endl;
        return Installed();
    }

    std::optional<State> operator()(Installing &, const event::ScriptOk &e) const {
        std::cout << "Installing -> Installed" << std::endl;
        return Installed();
    }

    std::optional<State> operator()(Installing &, const event::ScriptError &e) const {
        std::cout << "Installing -> ";
        if(s.installErrors.insert(); s.installErrors.isBroken()) {
            std::cout << "Broken" << std::endl;
            return Broken();
        } else {
            std::cout << "Installing" << std::endl;
            return Installing();
        }
    }

    std::optional<State> operator()(Installed &, const event::Update &error_rate) const {
        std::cout << "Installed -> ";
        if(s.stop) {
            std::cout << "Finished" << std::endl;
            return Finished();
        } else {
            if(s.dependencies_are_good()) {
                std::cout << "Startup" << std::endl;
                return StartingRun();
            } else {
                std::cout << "Installed" << std::endl;
                return {};
            }
        }
    }
    std::optional<State> operator()(Installed &, const event::Skip &) const {
        std::cout << "Installed -> Starting Run" << std::endl;
        return StartingRun();
    }

    std::optional<State> operator()(Broken &, const event::Update &e) const {
        if(s.stop) {
            std::cout << "Broken -> New\n";
            return New{};
        }
        return {};
    }

    std::optional<State> operator()(Startup &, const event::ScriptError &e) const {
        std::cout << "Starting -> Error -> ";
        if(s.startErrors.insert(); s.startErrors.isBroken()) {
            std::cout << "Broken" << std::endl;
            return Broken();
        } else {
            std::cout << "Installed" << std::endl;
            return Installed();
        }
    }

    std::optional<State> operator()(Startup &, const event::ScriptOk &e) const {
        std::cout << "Startup -> Running" << std::endl;
        return Running();
    }

    std::optional<State> operator()(Startup &, const event::Skip &e) const {
        std::cout << "Startup -> Starting Running" << std::endl;
        return StartingRun();
    }

    std::optional<State> operator()(StartingRun &, const event::Update &e) const {
        std::cout << "Starting Run -> Running" << std::endl;
        return Running();
    }

    std::optional<State> operator()(StartingRun &, const event::Skip &e) const {
        std::cout << "Starting Run -> Finished" << std::endl;
        return Finished();
    }

    std::optional<State> operator()(Running &, const event::Update &e) const {
        std::cout << "Running -> Stopping" << std::endl;
        return Stopping();
    }

    std::optional<State> operator()(Stopping &, const event::Skip &e) const {
        std::cout << "Stopping -> Finished (skip)" << std::endl;
        return Finished();
    }

    std::optional<State> operator()(Stopping &, const event::ScriptOk &e) const {
        std::cout << "Stopping -> KILL" << std::endl;
        return Kill();
    }

    std::optional<State> operator()(Stopping &, const event::ScriptError &e) const {
        std::cout << "Stopping -> KILL w/ Error" << std::endl;
        s.stopErrors.insert();
        return KillWStopError();
    }

    std::optional<State> operator()(Kill &, const event::Skip &e) const {
        std::cout << "Kill -> Finished";
        return Finished();
    }

    std::optional<State> operator()(KillWStopError &, const event::Skip &e) const {
        std::cout << "Kill w/ Stop Error ";
        if(s.stopErrors.isBroken()) {
            std::cout << "Broken" << std::endl;
            return Broken();
        } else {
            std::cout << "Finished" << std::endl;
            return Finished();
        }
    }

    std::optional<State> operator()(KillWRunError &, const event::Skip &e) const {
        std::cout << "Kill w/ Run Error -> ";
        if(s.stopErrors.isBroken()) {
            std::cout << "Broken" << std::endl;
            return Broken();
        } else {
            std::cout << "Finished" << std::endl;
            return Finished();
        }
    }

    std::optional<State> operator()(Finished &, const event::Update &e) const {
        if(s.restart || s.reinstall) {
            std::cout << "Finished -> Installed" << std::endl;
            return Installed();
        } else {
            return {};
        }
    }

    std::optional<State> operator()(StoppingWError &, const event::Skip &e) const {
        std::cout << "Stopping w/ Error -> Kill w/ Run Error" << std::endl;
        return KillWRunError{};
    }

    std::optional<State> operator()(StoppingWError &, const event::ScriptOk &e) const {
        std::cout << "Stopping w/ Error -> Kill w/ Run Error" << std::endl;
        return KillWRunError{};
    }

    std::optional<State> operator()(StoppingWError &, const event::ScriptError &e) const {
        std::cout << "Stopping w/ Error -> Kill w/ Run Error" << std::endl;
        return KillWRunError{};
    }

    // Default do-nothing
    template<typename State_t, typename Event_t>
    std::optional<State> operator()(State_t &, const Event_t &) const {
        std::cout << "Unknown" << std::endl;
        return {};
    }
};

class ComponentListener {
public:
    explicit ComponentListener(std::string name) noexcept : _name(std::move(name)) {
    }
    ComponentListener(const ComponentListener &) = default;
    ComponentListener(ComponentListener &&) = delete;
    ComponentListener &operator=(const ComponentListener &) = default;
    ComponentListener &operator=(ComponentListener &&) = delete;
    virtual ~ComponentListener() noexcept = default;
    [[nodiscard]] const std::string &getName() const noexcept {
        return _name;
    }

    virtual void skip(){};
    virtual void update(){};

    /* alerts to inform the system that we are in a state */
    virtual void alertStateChange(const State &previousState, const State &desiredState){};
    virtual void alertStateUnchanged(const State &currentState, const event::Event &event){};

private:
    std::string _name;
};

// template<
//     class Listener,
//     /* requires */ std::enable_if_t<std::is_base_of_v<ComponentListener, Listener>, int> = 0>
class ComponentLifecycle {
public:
    ComponentLifecycle(ComponentListener &listener, StateData &&initialState) noexcept
        : _listener{listener}, _stateData{std::move(initialState)} {
        std::visit([this](auto &&newState) { newState(_listener, _stateData); }, _currentState);
    };

    void dispatch(const event::Event &event) {
        std::optional<State> newState = std::visit(Transitions{_stateData}, _currentState, event);
        if(newState.has_value()) {
            _listener.alertStateChange(_currentState, newState.value());
            overrideState(newState.value());
        } else {
            _listener.alertStateUnchanged(_currentState, event);
        }
    }

    void scriptEvent(bool ok) {
        if(ok) {
            dispatch(event::ScriptOk{});
        } else {
            dispatch(event::ScriptError{});
        }
    }

    void setStop() {
        _stateData.stop = true;
    }

    void setStart() {
        _stateData.start = true;
    }

    void setRestart() {
        _stateData.restart = true;
    }

    void setReinstall() {
        _stateData.reinstall = true;
    }

    void clearFlags() noexcept {
        _stateData.reinstall = false;
        _stateData.stop = false;
        _stateData.start = false;
        _stateData.restart = false;
    }

    // Made public for testing
    void overrideState(State desiredState) {
        _currentState = desiredState;
        std::visit([this](auto &&newState) { newState(_listener, _stateData); }, _currentState);
    }

private:
    State _currentState{Initial{}};
    ComponentListener &_listener;
    StateData _stateData;
};
