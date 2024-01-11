// Main blocking thread, called by containing process
#include "lifecycle/command_line.hpp"
#include "scope/context_full.hpp"

extern "C" {
#include "nucleus_core.h"
}

// NOLINTNEXTLINE(*-avoid-c-arrays)
int ggapiMainThread(int argc, char *argv[], char *envp[]) noexcept {
    try {
        scope::Context &context = scope::Context::get();
        if(envp != nullptr) {
            context.sysProperties().parseEnv(envp);
        }
        lifecycle::Kernel kernel{context.baseRef()};
        // limited scope
        {
            lifecycle::CommandLine commandLine{context.baseRef(), kernel};
            commandLine.parseEnv(context.sysProperties());
            if(argc > 0 && argv != nullptr) {
                commandLine.parseArgs(argc, argv);
            }
            kernel.preLaunch(commandLine);
        }
        // Never returns unless signaled
        return kernel.launch();
    } catch(...) {
        // TODO: log errors
        std::terminate(); // terminate on exception
    }
}
