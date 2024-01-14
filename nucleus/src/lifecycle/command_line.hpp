#pragma once

#include "command_line_arguments.hpp"
#include "kernel.hpp"
#include "lifecycle/sys_properties.hpp"
#include "scope/context.hpp"
#include <list>
#include <optional>
#include <util.hpp>
#include <utility>

namespace lifecycle {

    class Kernel;

    class CommandLine : public scope::UsesContext {
    public:
        lifecycle::Kernel &_kernel;
        std::shared_ptr<util::NucleusPaths> _nucleusPaths;

        std::filesystem::path _providedConfigPath;
        std::filesystem::path _providedInitialConfigPath;
        std::string _awsRegionFromCmdLine;
        std::string _envStageFromCmdLine;
        std::string _defaultUserFromCmdLine;
        
        template<class V, class... T>
        static std::unique_ptr<argument> makeEntry(T &&...t) {
            auto ptr = std::unique_ptr<argument>(std::make_unique<V>(std::forward<T>(t)...));
            return ptr;
        };

        // HELP: I can't use auto unless argumentList is static and I want to remove the 7
        // I can't make argumentList static because the functors need the this pointer
        // We could pass this as an argument but then all the parameters need accessors or be public

        explicit CommandLine(const scope::UsingContext &context, lifecycle::Kernel &kernel)
            : scope::UsesContext(context), _kernel(kernel) {
        }

        void parseEnv(SysProperties &env);
        void parseHome(SysProperties &env);
        void parseRawProgramNameAndArgs(util::Span<char *>);
        void parseArgs(const std::vector<std::string> &args);

        void parseProgramName(std::string_view progName);

        static const std::unique_ptr<argument> argumentList[];

        void helpPrinter();

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
