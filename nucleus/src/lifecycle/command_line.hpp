#pragma once
#include "command_line_arguments.hpp"
#include "kernel.hpp"
#include "scope/context.hpp"
#include <list>

namespace lifecycle {

    class Kernel;

    class CommandLine {
    private:
        std::list<argument *> argumentList;

        std::weak_ptr<scope::Context> _context;
        lifecycle::Kernel &_kernel;
        std::shared_ptr<util::NucleusPaths> _nucleusPaths;

        std::filesystem::path _providedConfigPath;
        std::filesystem::path _providedInitialConfigPath;
        std::string _awsRegionFromCmdLine;
        std::string _envStageFromCmdLine;
        std::string _defaultUserFromCmdLine;

        [[nodiscard]] scope::Context &context() const {
            return *_context.lock();
        }

        static std::string nextArg(
            const std::vector<std::string> &args, std::vector<std::string>::const_iterator &iter);

    public:
        explicit CommandLine(
            const std::shared_ptr<scope::Context> &context, lifecycle::Kernel &kernel);

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
