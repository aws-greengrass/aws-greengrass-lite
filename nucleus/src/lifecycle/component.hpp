/* Note: This needs a better name, but this is good enough for now */

/* this module will execute the state machine according the following business logic
    1) Start a component at the earliest opportunity (when dependencies are running)
    2) Restart the component when configuration parameters change (need a sensitivity list for
   restart)
   3) Reinstall the component hen configuration parameters change (need a sensitivity list
   for reinstall)
   4) Stop the component "correctly" */

#include "lifecycle_fsm.hpp"

class component {
    component(
        std::string_view name,
        scriptRunner &installRunner,
        scriptRunner &startupRunner,
        scriptRunner &runRunner,
        scriptRunner &shutdwonRunner)
        : _fsm(theData, installRunner, startupRunner, runRunner, shutdownRunner),
          theData(
              name, [this]() { sendSkipEvent(); }, [this]() { sendUpdateEvent(); }){

              /* register a state observer with each of my dependencies */

          };

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

    void alert() {
        /* inform all of my observers of my state */
    }

private:
    void sendSkipEvent() {
        _fsm.dispatch(eventSkip{});
    }
    void sendUpdateEvent() {
        _fsm.dispatch(eventUpdate{});
    }

    component_data theData;
    componentLifecycle _fsm;
};
