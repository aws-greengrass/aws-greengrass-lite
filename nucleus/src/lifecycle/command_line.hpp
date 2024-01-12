#pragma once

#include "command_line_arguments.hpp"
#include "kernel.hpp"
#include "lifecycle/sys_properties.hpp"
#include "scope/context.hpp"
#include <list>
#include <optional>
#include <util.hpp>

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

        template<typename V, typename... T>
        constexpr auto array_of(T &&...t) -> std::array<V, sizeof...(T)> {
            return {{std::forward<T>(t)...}};
        }

        std::array<std::unique_ptr<argument>, 7> argumentList{
            std::unique_ptr<argument>(std::make_unique<argumentFlag>(
                "h",
                "help",
                "Print this usage information",
                [this]() {
                    for(const auto &a : argumentList) {
                        std::cout << a->getDescription() << std::endl;
                    }
                    std::exit(0);
                })),
            std::unique_ptr<argument>(std::make_unique<argumentValue<std::string>>(
                "i",
                "config",
                "configuration Path",
                [this](std::string arg) {
                    _providedConfigPath = _kernel.getPaths()->deTilde(arg);
                })),
            std::unique_ptr<argument>(std::make_unique<argumentValue<std::string>>(
                "init",
                "init-config",
                "initial configuration path",
                [this](std::string arg) {
                    _providedInitialConfigPath = _kernel.getPaths()->deTilde(arg);
                })),
            std::unique_ptr<argument>(std::make_unique<argumentValue<std::string>>(
                "r",
                "root",
                "the root path selection",
                [this](std::string arg) {
                    auto paths = _kernel.getPaths();
                    paths->setRootPath(paths->deTilde(arg));
                })),
            std::unique_ptr<argument>(std::make_unique<argumentValue<std::string>>(
                "ar",
                "aws-region",
                "AWS Region",
                [this](std::string arg) { _awsRegionFromCmdLine = arg; })),
            std::unique_ptr<argument>(std::make_unique<argumentValue<std::string>>(
                "es",
                "env-stage",
                "Environment Stage Selection",
                [this](std::string arg) { _envStageFromCmdLine = arg; })),
            std::unique_ptr<argument>(std::make_unique<argumentValue<std::string>>(
                "u",
                "component-default-user",
                "Component Default User",
                [this](std::string arg) { _defaultUserFromCmdLine = arg; })),
        };

    public:
        explicit CommandLine(const scope::UsingContext &context, lifecycle::Kernel &kernel)
            : scope::UsesContext(context), _kernel(kernel) {
        }

        void parseEnv(SysProperties &env);
        void parseHome(SysProperties &env);
        void parseRawProgramNameAndArgs(util::Span<char *>);
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
