#include "lifecycle_fsm.hpp"

/* state worker functions.  These functions are called once when a state transition occurs.
 * If the state transition is returning to the same state, the function is called again.
 * If the state transition is "unchanged" this function is not called.
 */

void Initial::operator()(component_data &l, state_data &s) {
    std::cout << l.getName() << ": " << getName() << " Entry" << std::endl;
}

void New::operator()(component_data &l, state_data &s) {
    std::cout << l.getName() << ": " << getName() << " Entry" << std::endl;
    l.alertNEW();
    s.stop = false;
    l.update(); /* allow continuation if !stop */
}

void Installing::operator()(component_data &l, state_data &s) {
    std::cout << l.getName() << ": " << getName() << " Entry" << std::endl;

    if(s.installScript->willRun()) {
        s.installScript->start();
    } else {
        l.skip(); /* the script will not run so jump to the next state */
    }
}

void Installed::operator()(component_data &l, state_data &s) {
    std::cout << l.getName() << ": " << getName() << " Entry" << std::endl;
    l.alertINSTALLED();
    s.reinstall = false;
    l.update(); /* allow continuation if !reinstall */
}

void Broken::operator()(component_data &l, state_data &s) {
    std::cout << l.getName() << ": " << getName() << " Entry" << std::endl;
    l.alertBROKEN();
    s.stop = false;
    l.update(); /* allow continuation if !stop */
}

void Startup::operator()(component_data &l, state_data &s) {
    std::cout << l.getName() << ": " << getName() << " Entry" << std::endl;
    if(s.startScript->willRun()) {
        s.startScript->start();
    } else {
        l.skip(); /* script will not run so jump to the next state */
    }
}

void StartingRun::operator()(component_data &l, state_data &s) {
    std::cout << l.getName() << ": " << getName() << " Entry" << std::endl;
    if(s.runScript->willRun()) {
        s.runScript->start();
        l.update(); /* move on to the running state */
    } else {
        l.skip(); /* skip to the Finished state */
    }
}

void Running::operator()(component_data &l, state_data &s) {
    std::cout << l.getName() << ": " << getName() << " Entry" << std::endl;
    l.alertRUNNING();
    if(s.runScript) {
        if(s.runScript->willRun()) {
            s.runScript->start();
        }
    }
}

void Stopping::operator()(component_data &l, state_data &s) {
    std::cout << l.getName() << ": " << getName() << " Entry" << std::endl;
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

void StoppingWError::operator()(component_data &l, state_data &s) {
    std::cout << l.getName() << ": " << getName() << " Entry" << std::endl;
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

void Finished::operator()(component_data &l, state_data &s) {
    std::cout << l.getName() << ": " << getName() << " Entry" << std::endl;
    l.alertFINISHED();
    s.stop = false;
}

void Kill::operator()(component_data &l, state_data &s) {
    std::cout << l.getName() << ": " << getName() << " Entry" << std::endl;
    s.killAll();
    l.skip();
}

void KillWRunError::operator()(component_data &l, state_data &s) {
    std::cout << l.getName() << ": " << getName() << " Entry" << std::endl;
    s.killAll();
    l.skip();
}

void KillWStopError::operator()(component_data &l, state_data &s) {
    std::cout << l.getName() << ": " << getName() << " Entry" << std::endl;
    s.killAll();
    l.skip();
}
