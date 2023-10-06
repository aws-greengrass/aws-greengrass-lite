#pragma once
#include "../data/globals.h"
#include <optional>

namespace lifecycle {

    class KernelCommandLine {
    private:
        data::Global &_global;
        mutable std::shared_mutex _mutex;
        static constexpr auto HOME_DIR_PREFIX{"~/"};
        static constexpr auto ROOT_DIR_PREFIX{"~root/"};
        static constexpr auto CONFIG_DIR_PREFIX{"~config/"};
        static constexpr auto PACKAGE_DIR_PREFIX{"~packages/"};

        std::filesystem::path _userHomeDir;
        std::filesystem::path _programRootDir;
        std::filesystem::path _providedConfigPathName;
        std::filesystem::path _providedInitialConfigPath;
        std::string _awsRegionFromCmdLine;
        std::string _envStageFromCmdLine;
        std::string _defaultUserFromCmdLine;

        std::string _configPathName{"~root/config"};
        std::string _workPathName{"~root/work"};
        std::string _packageStorePathName{"~root/packages"};
        std::string _kernelAltsPathName{"~root/alts"};
        std::string _deploymentsPathName{"~root/deployments"};
        std::string _cliIpcInfoPathName{"~root/cli_ipc_info"};
        std::string _binPathName{"~root/bin"};

        static std::string nextArg(
            const std::vector<std::string> &args, std::vector<std::string>::const_iterator &iter
        );
        std::filesystem::path deTilde(std::string_view s) const; // assumes lock
                                                                 // held
        static std::filesystem::path resolve(
            const std::filesystem::path &first, const std::filesystem::path &second
        );
        static std::filesystem::path resolve(
            const std::filesystem::path &first, std::string_view second
        );

    public:
        explicit KernelCommandLine(data::Global &global) : _global{global} {
        }

        int main();
        void parseEnv(data::SysProperties &sysProperties);
        void parseHome(data::SysProperties &sysProperties);
        void parseArgs(int argc, char *argv[]); // NOLINT(*-avoid-c-arrays)
        void parseArgs(const std::vector<std::string> &args);
        void parseProgramName(std::string_view progName);
    };
} // namespace lifecycle
