//
//  scripting.hpp
//  fsm
//
//  Created by Julicher, Joe on 2/3/24.
//

#pragma once

struct scriptRunner{
    bool willRun(){return false;}     /* check the recipe and determine if the script is allowed to run. */
    bool start(){                    /* start the script running if it is allowed.  Return TRUE if the script is started and false otherwise */
        std::cout << "script starting" << std::endl;
        return true;
    }
    void kill(){}                    /* issue sigTERM/sigKILL to the script process to ensure it is truly dead */
    bool isOK(){return true;}        /* return true if the script has no errors (even if it is running).  Return false if there are any errors. */
    bool isRunning(){return false;}  /* return if the script is running. Use isOK to determine if the script has errors */
};

