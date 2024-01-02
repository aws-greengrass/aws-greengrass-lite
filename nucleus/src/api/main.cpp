// Main blocking thread, called by containing process
#include "lifecycle/command_line.hpp"
#include "scope/context_full.hpp"

extern "C" {
#include "nucleus_core.h"
}

// NOLINTNEXTLINE(*-avoid-c-arrays)
int ggapiMainThread(int argc, char *argv[], char *envp[]) noexcept {
    try {
        auto context = scope::context();
        if(envp != nullptr) {
            auto end = envp;
            while(*end != nullptr) {
                ++end;
            }
            context->sysProperties().parseEnv({envp, static_cast<size_t>(end - envp)});
        }
        lifecycle::Kernel kernel{context};
        // limited scope
        {
            lifecycle::CommandLine commandLine{context, kernel};
            commandLine.parseEnv(context->sysProperties());
            if(argc > 0 && argv != nullptr) {
                commandLine.parseRawProgramNameAndArgs({argv, static_cast<size_t>(argc)});
            }
            kernel.preLaunch(commandLine);
        }
        // Never returns unless signalled
        return kernel.launch();
    } catch(...) {
        // TODO: log errors
        std::terminate(); // terminate on exception
    }
}
