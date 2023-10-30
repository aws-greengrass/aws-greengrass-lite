#pragma once
#include "deployment/deployment_model.hpp"
#include "util/nucleus_paths.hpp"

namespace deployment {}

namespace lifecycle {

    class KernelAlternatives {
        std::shared_ptr<util::NucleusPaths> _nucleusPaths{};

        std::filesystem::path getAltsDir();
        std::filesystem::path getLoaderPathFromLaunchDir(std::filesystem::path);

        bool validateLaunchDirSetup(std::filesystem::path);

        void cleanupLaunchDirectoryLink(std::filesystem::path);
        void cleanupLaunchDirectorySingleLevel(std::filesystem::path);

    public:
        explicit KernelAlternatives(std::shared_ptr<util::NucleusPaths> nucleusPaths);

        static constexpr auto KERNEL_DISTRIBUTION_DIR{"distro"};
        static constexpr auto KERNEL_BIN_DIR{"bin"};
        static constexpr auto LAUNCH_PARAMS_FILE{"launch.params"};

        std::filesystem::path getLaunchParamsPath();

        void writeLaunchParamsToFile(std::string content);
        bool isLaunchDirSetup();
        void validateLaunchDirSetupVerbose();
        void setupInitLaunchDirIfAbsent();

        void relinkInitLaunchDir(std::filesystem::path, bool);
        std::filesystem::path locateCurrentKernelUnpackDir();

        deployment::DeploymentStage determineDeploymentStage();
        void activationSucceeds();
        void prepareRollback();
        void rollbackCompletes();
        void prepareBootstrap(std::string);
        void setupLinkToDirectory(std::filesystem::path, std::filesystem::path);

        void cleanupLaunchDirectoryLinks();
    };

} // namespace lifecycle
