// Main blocking thread, called by containing process
#include "lifecycle/kernel_command_line.h"
#include <c_api.h>

int ggapiMainThread(int argc, char *argv[], char *envp[]) noexcept {
    try {
        data::Global &global = data::Global::self();
        if(envp != nullptr) {
            global.environment.sysProperties.parseEnv(envp);
        }
        lifecycle::KernelCommandLine kernel{global};
        kernel.parseEnv(global.environment.sysProperties);
        if(argc > 0 && argv != nullptr) {
            kernel.parseArgs(argc, argv);
        }
        return kernel.main();
    } catch(...) {
        // TODO: log errors
        std::terminate(); // terminate on exception
    }
}
