// Main blocking thread, called by containing process
#include "lifecycle/command_line.hpp"
#include "nucleus_core.hpp"

// NOLINTNEXTLINE(*-avoid-c-arrays)
int ggapiMainThread(int argc, char *argv[], char *envp[]) noexcept {
    try {
        data::Global &global = data::Global::self();
        if(envp != nullptr) {
            global.environment.sysProperties.parseEnv(envp);
        }
        lifecycle::Kernel kernel{global};
        // limited scope
        {
            lifecycle::CommandLine commandLine{global, kernel};
            commandLine.parseEnv(global.environment.sysProperties);
            if(argc > 0 && argv != nullptr) {
                commandLine.parseArgs(argc, argv);
            }
            kernel.preLaunch(commandLine);
        }
        // Never returns unless signalled
        kernel.launch();
        return 0; // never reached
    } catch(...) {
        // TODO: log errors
        std::terminate(); // terminate on exception
    }
}
