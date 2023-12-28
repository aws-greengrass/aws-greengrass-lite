#pragma once
#include "kernel.hpp"
#include "lifecycle/sys_properties.hpp"
#include "scope/context.hpp"
#include <optional>

namespace lifecycle {

    class Kernel;

    class CommandLine : public scope::UsesContext {
    private:
        lifecycle::Kernel &_kernel;
        std::shared_ptr<util::NucleusPaths> _nucleusPaths;

        std::filesystem::path _providedConfigPath;
        std::filesystem::path _providedInitialConfigPath;
        std::string _awsRegionFromCmdLine;
        std::string _envStageFromCmdLine;
        std::string _defaultUserFromCmdLine;

        static std::string nextArg(
            const std::vector<std::string> &args, std::vector<std::string>::const_iterator &iter);

    public:
        explicit CommandLine(const scope::UsingContext &context, lifecycle::Kernel &kernel)
            : scope::UsesContext(context), _kernel(kernel) {
        }

        void parseEnv(SysProperties &sysProperties);
        void parseHome(SysProperties &sysProperties);
        void parseArgs(int argc, char *argv[]); // NOLINT(*-avoid-c-arrays)
        void parseArgs(const std::vector<std::string> &args);
        void parseProgramName(std::string_view progName);

        std::string getAwsRegion() {
            return _awsRegionFromCmdLine;
        }

        std::string getEnvStage() {
            return _envStageFromCmdLine;
        }

        std::string getDefaultUser() {
            return _defaultUserFromCmdLine;
        }

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
