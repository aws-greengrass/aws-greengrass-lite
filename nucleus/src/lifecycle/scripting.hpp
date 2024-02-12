#pragma once

#include "data/shared_struct.hpp"
#include <functional>
#include <iostream>

class scriptRunner {
public:
    scriptRunner(
        ggapi::Struct scriptConfig, std::function<void(bool)> eventSender, int timeout_seconds)
        : _eventSender(eventSender), _timeout_seconds(timeout_seconds){};

    bool willRun(); /* check the recipe and determine if the script is allowed to run. */
    bool start(); /* start the script running if it is allowed.  Return TRUE if the script is
                     started and false otherwise.  When the script completes call _eventSender with
                     the completion status. */
    void kill(); /* issue sigTERM/sigKILL to the script process to ensure it is truly dead */
private:
    std::function<void(bool)> _eventSender;
    int _timeout_seconds;
};
