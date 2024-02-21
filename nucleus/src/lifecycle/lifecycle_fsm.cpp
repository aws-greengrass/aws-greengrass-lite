#include "lifecycle_fsm.hpp"
#include <variant>

/* state worker functions.  These functions are called once when a state transition occurs.
 * If the state transition is returning to the same state, the function is called again.
 * If the state transition is "unchanged" this function is not called.
 */

// Prints the component name and state name as-if entering state
static void printStateEntry(std::string_view stateName, std::string_view componentName) noexcept {
    std::cout << componentName << ": " << stateName << " Entry\n";
}

void Initial::operator()(ComponentListener &l, StateData &s) {
    printStateEntry(name, l.getName());
}

void New::operator()(ComponentListener &l, StateData &s) {
    printStateEntry(name, l.getName());
    l.alertNEW();
    s.stop = false;
    l.update(); /* allow continuation if !stop */
}

void Installing::operator()(ComponentListener &l, StateData &s) {
    printStateEntry(name, l.getName());
    if(s.installScript.has_value() && s.installScript->willRun()) {
        s.installScript->start();
    } else {
        l.skip(); /* the script will not run so jump to the next state */
    }
}

void Installed::operator()(ComponentListener &l, StateData &s) {
    printStateEntry(name, l.getName());
    l.alertINSTALLED();
    s.reinstall = false;
    l.update(); /* allow continuation if !reinstall */
}

void Broken::operator()(ComponentListener &l, StateData &s) {
    printStateEntry(name, l.getName());
    l.alertBROKEN();
    s.stop = false;
    l.update(); /* allow continuation if !stop */
}

void Startup::operator()(ComponentListener &l, StateData &s) {
    printStateEntry(name, l.getName());
    if(s.startScript.has_value() && s.startScript->willRun()) {
        s.startScript->start();
    } else {
        l.skip(); /* script will not run so jump to the next state */
    }
}

void StartingRun::operator()(ComponentListener &l, StateData &s) {
    printStateEntry(name, l.getName());
    if(s.runScript.has_value() && s.runScript->willRun()) {
        s.runScript->start();
        l.update(); /* move on to the running state */
    } else {
        l.skip(); /* skip to the Finished state */
    }
}

void Running::operator()(ComponentListener &l, StateData &s) {
    printStateEntry(name, l.getName());
    l.alertRUNNING();
    if(s.runScript) {
        if(s.runScript->willRun()) {
            s.runScript->start();
        }
    }
}

void Stopping::operator()(ComponentListener &l, StateData &s) {
    printStateEntry(name, l.getName());
    l.alertSTOPPING();
    if(s.shutdownScript) {
        if(s.shutdownScript->willRun()) {
            s.shutdownScript->start();
        } else {
            l.skip();
        }
    } else {
        l.skip();
    }
}

void StoppingWError::operator()(ComponentListener &l, StateData &s) {
    printStateEntry(name, l.getName());
    l.alertERROR();
    if(s.shutdownScript) {
        if(s.shutdownScript->willRun()) {
            s.shutdownScript->start();
        } else {
            l.skip();
        }
    } else {
        l.skip();
    }
}

void Finished::operator()(ComponentListener &l, StateData &s) {
    printStateEntry(name, l.getName());
    l.alertFINISHED();
    s.stop = false;
}

void Kill::operator()(ComponentListener &l, StateData &s) {
    printStateEntry(name, l.getName());
    s.abort();
    l.skip();
}

void KillWRunError::operator()(ComponentListener &l, StateData &s) {
    printStateEntry(name, l.getName());
    s.abort();
    l.skip();
}

void KillWStopError::operator()(ComponentListener &l, StateData &s) {
    printStateEntry(name, l.getName());
    s.abort();
    l.skip();
}
