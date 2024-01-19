#pragma once

#include "command_line_arguments.hpp"
#include "kernel.hpp"
#include "sys_properties.hpp"

#include <scope/context.hpp>
#include <util.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace lifecycle {

    class Kernel;

    class CommandLine final : public scope::UsesContext {
    private:
        static constexpr std::string_view DEFAULT_POSIX_USER = "ggc_user:ggc_group";

        lifecycle::Kernel &_kernel;
        std::shared_ptr<util::NucleusPaths> _nucleusPaths;

        std::filesystem::path _providedConfigPath;
        std::filesystem::path _providedInitialConfigPath;
        std::string _awsRegionFromCmdLine;
        std::string _envStageFromCmdLine;
        std::string _defaultUserFromCmdLine;

    public:
        explicit CommandLine(const scope::UsingContext &context, lifecycle::Kernel &kernel)
            : scope::UsesContext(context), _kernel(kernel) {
        }

        void parseEnv(SysProperties &env);
        void parseHome(SysProperties &env);
        void parseRawProgramNameAndArgs(util::Span<char *>);
        void parseArgs(const std::vector<std::string> &args);

        void parseProgramName(std::string_view progName);

        [[noreturn]] static void helpPrinter();

        [[nodiscard]] Kernel &getKernel() noexcept {
            return _kernel;
        }

        [[nodiscard]] std::string getAwsRegion() const {
            return _awsRegionFromCmdLine;
        }

        [[nodiscard]] std::string getEnvStage() const {
            return _envStageFromCmdLine;
        }

        [[nodiscard]] std::string getDefaultUser() const {
            return _defaultUserFromCmdLine;
        }

        [[nodiscard]] std::filesystem::path getProvidedConfigPath() const {
            return _providedConfigPath;
        }

        [[nodiscard]] std::filesystem::path getProvidedInitialConfigPath() const {
            return _providedInitialConfigPath;
        }

        void setProvidedConfigPath(std::filesystem::path path) noexcept {
            _providedConfigPath = std::move(path);
        }

        void setDefaultUser(std::string user) noexcept {
            _defaultUserFromCmdLine = std::move(user);
        }

        void setEnvStage(std::string stage) noexcept {
            _envStageFromCmdLine = std::move(stage);
        }

        void setAwsRegion(std::string region) noexcept {
            _awsRegionFromCmdLine = std::move(region);
        }

        void setProvidedInitialConfigPath(std::filesystem::path path) noexcept {
            _providedInitialConfigPath = std::move(path);
        }
    };

} // namespace lifecycle
