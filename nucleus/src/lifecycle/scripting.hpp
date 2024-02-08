#pragma once
#include <iostream>

struct scriptRunner {
    bool willRun(); /* check the recipe and determine if the script is allowed to run. */
    bool start(); /* start the script running if it is allowed.  Return TRUE if the script is
                     started and false otherwise */
    void kill(); /* issue sigTERM/sigKILL to the script process to ensure it is truly dead */
    bool isOK(); /* return true if the script has no errors (even if it is running).  Return false
      if there are any errors. */
    bool isRunning(); /* return if the script is running. Use isOK to determine if the script has
                         errors */
};
