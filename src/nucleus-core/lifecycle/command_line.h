#pragma once
#include "data/globals.h"
#include "kernel.h"
#include <optional>

namespace lifecycle {

    class Kernel;

    class CommandLine {
    private:
        data::Global &_global;
        lifecycle::Kernel &_kernel;

        std::filesystem::path _providedConfigPath;
        std::filesystem::path _providedInitialConfigPath;
        std::string _awsRegionFromCmdLine;
        std::string _envStageFromCmdLine;
        std::string _defaultUserFromCmdLine;

        static std::string nextArg(
            const std::vector<std::string> &args, std::vector<std::string>::const_iterator &iter
        );

    public:
        explicit CommandLine(data::Global &global, lifecycle::Kernel &kernel)
            : _global(global), _kernel(kernel) {
        }

        void parseEnv(data::SysProperties &sysProperties);
        void parseHome(data::SysProperties &sysProperties);
        void parseArgs(int argc, char *argv[]); // NOLINT(*-avoid-c-arrays)
        void parseArgs(const std::vector<std::string> &args);
        void parseProgramName(std::string_view progName);

        std::filesystem::path getProvidedConfigPath() {
            return _providedConfigPath;
        }

        std::filesystem::path getProvidedInitialConfigPath() {
            return _providedInitialConfigPath;
        }

        void setProvidedConfigPath(const std::filesystem::path &path) {
            _providedConfigPath = path;
        }
    };
} // namespace lifecycle
