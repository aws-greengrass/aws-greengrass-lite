#pragma once

#include <algorithm>
#include <iostream>
#include <optional>
#include <variant>

#include <error_rate.hpp>

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

class component_data;

class state_data {
public:
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
struct Startup : state_base { /* Startup will try to execute the startup script */
    void operator()(component_data &, state_data &);
    std::string_view getName() {
        return "Starting";
    }
};
struct StartingRun : state_base { /* starting run will try to execute the run script */
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
        std::cout << "Installed -> ";
        if(s.stop) {
            std::cout << "Finished" << std::endl;
            return Finished();
        } else {
            if(dependencies_are_good()) {
                std::cout << "Startup" << std::endl;
                return Startup();
            } else {
                std::cout << "Installed" << std::endl;
                return {};
            }
        }
    }
    std::optional<State> operator()(Startup &, const eventSkip &, state_data &s) {
        std::cout << "Installed -> Starting Run" << std::endl;
        return StartingRun();
    }

    std::optional<State> operator()(Startup &, const eventScriptEventError &e, state_data &s) {
        std::cout << "Starting -> Error -> ";
        s.startErrors.newError();
        if(s.startErrors.isBroken()) {
            std::cout << "Broken" << std::endl;
            return Broken();
        } else {
            std::cout << "Installed" << std::endl;
            return Installed();
        }
    }

    std::optional<State> operator()(Startup &, const eventScriptEventOK &e, state_data &s) {
        std::cout << "Startup -> Running" << std::endl;
        return Running();
    }

    std::optional<State> operator()(Startup &, const eventSkip &e, state_data &s) {
        std::cout << "Startup -> Starting Running" << std::endl;
        return StartingRun();
    }

    std::optional<State> operator()(StartingRun &, const eventUpdate &e, state_data &s) {
        std::cout << "Starting Run -> Running" << std::endl;
        return Running();
    }

    std::optional<State> operator()(StartingRun &, const eventSkip &e, state_data &s) {
        std::cout << "Starting Run -> Finished" << std::endl;
        return Finished();
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
class lifecycle {
public:
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

using componentLifecycle = lifecycle<SAtate, Event, Transitions>;

class component_data {
public:
    component_data(
        std::string_view name,
        std::function<void(void)> skipSender,
        std::function<void(void)> updateSender,
        std::function<void(void)> alertNew,
        std::function<void(void)> alertInstalled,
        std::function<void(void)> alertRunning,
        std::function<void(void)> alertStopping,
        std::function<void(void)> alertError,
        std::function<void(void)> alertBroken,
        std::function<void(void)> alertFinished)
        : _name(name), _skipper(skipSender), _updater(updateSender), _alertNew(alertNew),
          _alertInstalled(alertInstalled), _alertRunning(alertRunning),
          _alertStopping(alertStopping), _alertError(alertError), _alertBroken(alertBroken),
          _alertFinished(alertFinished){};

    void skip() {
        _skipper();
    }
    void update() {
        _updater();
    }
    std::string_view getName() {
        return _name;
    }

    inline void alertNEW() {
        _alertNew();
    }
    inline void alertINSTALLED() {
        _alertInstalled();
    }
    inline void alertRUNNING() {
        _alertRunning();
    }
    inline void alertSTOPPING() {
        _alertStopping();
    }
    inline void alertERROR() {
        _alertError();
    }
    inline void alertBROKEN() {
        _alertBroken();
    }
    inline void alertFINISHED() {
        _alertFinished();
    }

private:
    std::string _name;
    std::function<void(void)> _skipper;
    std::function<void(void)> _updater;

    /* alerts to inform the system that we are in a state */
    std::function<void(void)> _alertNew;
    std::function<void(void)> _alertInstalled;
    std::function<void(void)> _alertRunning;
    std::function<void(void)> _alertStopping;
    std::function<void(void)> _alertError;
    std::function<void(void)> _alertBroken;
    std::function<void(void)> _alertFinished;
};
