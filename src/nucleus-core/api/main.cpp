// Main blocking thread, called by containing process
#include "lifecycle/command_line.h"
#include <c_api.h>

int ggapiMainThread(int argc, char *argv[], char *envp[]) noexcept {
    try {
        data::Global &global = data::Global::self();
        if(envp != nullptr) {
            global.environment.sysProperties.parseEnv(envp);
        }
<<<<<<< HEAD
        lifecycle::KernelCommandLine kernel{global};
        kernel.parseEnv(global.environment.sysProperties);
        if(argc > 0 && argv != nullptr) {
            kernel.parseArgs(argc, argv);
=======
        lifecycle::Kernel kernel{global};
        // limited scope
        {
            lifecycle::CommandLine commandLine{global, kernel};
            commandLine.parseEnv(global.environment.sysProperties);
            if (argc > 0 && argv != nullptr) {
                commandLine.parseArgs(argc, argv);
            }
            kernel.preLaunch(commandLine);
>>>>>>> 3fc2320 (Nucleus bootup-and-read-config procedure ported from GG-Java)
        }
        // Never returns unless signalled
        kernel.launch();
        return 0; // never reached
    } catch(...) {
        // TODO: log errors
        std::terminate(); // terminate on exception
    }
}
