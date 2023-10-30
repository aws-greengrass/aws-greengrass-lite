#pragma once
#include "deployment/deployment_model.hpp"
#include "util/nucleus_paths.hpp"

namespace deployment {}

namespace lifecycle {

    class KernelAlternatives {
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
