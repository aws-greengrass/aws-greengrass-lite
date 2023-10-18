#pragma once
#include "deployment/deployment_model.hpp"
#include "util/nucleus_paths.hpp"

namespace deployment {}

namespace lifecycle {

    class KernelAlternatives {
        static constexpr auto CURRENT_DIR{"current"};
        static constexpr auto OLD_DIR{"old"};
        static constexpr auto NEW_DIR{"new"};
        static constexpr auto BROKEN_DIR{"broken"};
        static constexpr auto INITIAL_SETUP_DIR{"init"};
        static constexpr auto KERNEL_LIB_DIR{"lib"};
        static constexpr auto LOADER_PID_FILE{"loader.pid"};

        std::shared_ptr<util::NucleusPaths> _nucleusPaths;

        std::filesystem::path getAltsDir();
        std::filesystem::path getLoaderPathFromLaunchDir(const std::filesystem::path);

        bool validateLaunchDirSetup(const std::filesystem::path);

        void cleanupLaunchDirectoryLink(const std::filesystem::path);
        void cleanupLaunchDirectorySingleLevel(const std::filesystem::path);

    public:
        explicit KernelAlternatives(std::shared_ptr<util::NucleusPaths> nucleusPaths);

        static constexpr auto KERNEL_DISTRIBUTION_DIR{"distro"};
        static constexpr auto KERNEL_BIN_DIR{"bin"};
        static constexpr auto LAUNCH_PARAMS_FILE{"launch.params"};

        std::filesystem::path getBrokenDir();
        std::filesystem::path getOldDir();
        std::filesystem::path getNewDir();
        std::filesystem::path getCurrentDir();
        std::filesystem::path getInitDir();
        std::filesystem::path getLoaderPidPath();
        std::filesystem::path getLoaderPath();
        std::filesystem::path getBinDir();
        std::filesystem::path getLaunchParamsPath();

        void writeLaunchParamsToFile(const std::string content);
        bool isLaunchDirSetup();
        void validateLaunchDirSetupVerbose();
        void setupInitLaunchDirIfAbsent();

        void relinkInitLaunchDir(const std::filesystem::path, bool);
        std::filesystem::path locateCurrentKernelUnpackDir();

        deployment::DeploymentStage determineDeploymentStage();
        void activationSucceeds();
        void prepareRollback();
        void rollbackCompletes();
        void prepareBootstrap(const std::string);
        void setupLinkToDirectory(const std::filesystem::path, const std::filesystem::path);

        void cleanupLaunchDirectoryLinks();
    };

} // namespace lifecycle
