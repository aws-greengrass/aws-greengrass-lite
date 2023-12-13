#pragma once

#include "util/permissions.hpp"
#include <filesystem>
#include <mutex>
#include <shared_mutex>

namespace util {
    //
    // GG-Java has these in Util, but probably should be somewhere else
    //
    class NucleusPaths {
        Permissions _permissions;
        mutable std::shared_mutex _mutex;
        std::filesystem::path _homePath;
        std::filesystem::path _rootPath;
        std::filesystem::path _workPath;
        std::filesystem::path _componentStorePath;
        std::filesystem::path _configPath;
        std::filesystem::path _deploymentPath;
        std::filesystem::path _kernelAltsPath;
        std::filesystem::path _cliIpcInfoPath;
        std::filesystem::path _binPath;
        static constexpr auto HOME_DIR_PREFIX{"~/"};
        static constexpr auto ROOT_DIR_PREFIX{"~root/"};
        static constexpr auto CONFIG_DIR_PREFIX{"~config/"};
        static constexpr auto PACKAGE_DIR_PREFIX{"~packages/"};

    public:
        static constexpr auto PLUGINS_DIRECTORY{"plugins"};
        static constexpr auto ARTIFACT_DIRECTORY{"artifacts"};
        static constexpr auto RECIPE_DIRECTORY{"recipes"};
        static constexpr auto DEFAULT_LOGS_DIRECTORY{"logs"};
        static constexpr auto ARTIFACTS_DECOMPRESSED_DIRECTORY{"artifacts-unarchived"};
        static constexpr auto CONFIG_PATH_NAME{"config"};
        static constexpr auto WORK_PATH_NAME{"work"};
        static constexpr auto PACKAGES_PATH_NAME{"packages"};
        static constexpr auto ALTS_PATH_NAME{"alts"};
        static constexpr auto DEPLOYMENTS_PATH_NAME{"deployments"};
        static constexpr auto CLI_IPC_INFO_PATH_NAME{"cli_ipc_info"};
        static constexpr auto BIN_PATH_NAME{"bin"};
        static constexpr auto CURRENT_DIR{"current"};
        static constexpr auto OLD_DIR{"old"};
        static constexpr auto NEW_DIR{"new"};
        static constexpr auto BROKEN_DIR{"broken"};
        static constexpr auto INITIAL_SETUP_DIR{"init"};
        static constexpr auto KERNEL_LIB_DIR{"lib"};
        static constexpr auto LOADER_PID_FILE{"loader.pid"};

        static void createPath(const std::filesystem::path &path);

        std::filesystem::path deTilde(std::string_view s) const;
        static std::filesystem::path resolve(
            const std::filesystem::path &first, const std::filesystem::path &second);
        static std::filesystem::path resolve(
            const std::filesystem::path &first, std::string_view second);
        static std::filesystem::path resolveRelative(std::string_view path);
        static std::filesystem::path resolve(std::string_view path);
        static std::filesystem::path resolve(const std::filesystem::path &path);
        void initPaths(std::string_view rootPathString);

        NucleusPaths &setHomePath(const std::filesystem::path &newPath) {
            std::unique_lock guard{_mutex};
            _homePath = newPath;
            guard.unlock();
            createPath(newPath);
            return *this;
        }

        [[nodiscard]] std::filesystem::path homePath() const {
            std::shared_lock guard{_mutex};
            return _homePath;
        }

        NucleusPaths &setBinPath(const std::filesystem::path &newPath, bool passive = false) {
            std::unique_lock guard{_mutex};
            _binPath = newPath;
            guard.unlock();
            if(!passive) {
                createPath(newPath); // should be a no-op on GG-lite
                _permissions.setBinPermission(newPath);
            }
            return *this;
        }

        [[nodiscard]] std::filesystem::path binPath() const {
            std::shared_lock guard{_mutex};
            return _binPath;
        }

        NucleusPaths &setCliIpcInfoPath(const std::filesystem::path &newPath) {
            std::unique_lock guard{_mutex};
            _cliIpcInfoPath = newPath;
            guard.unlock();
            createPath(newPath);
            _permissions.setCliIpcInfoPermission(newPath);
            return *this;
        }

        [[nodiscard]] std::filesystem::path cliIpcInfoPath() const {
            std::shared_lock guard{_mutex};
            return _cliIpcInfoPath;
        }

        NucleusPaths &setKernelAltsPath(const std::filesystem::path &newPath) {
            std::unique_lock guard{_mutex};
            _kernelAltsPath = newPath;
            guard.unlock();
            createPath(newPath);
            _permissions.setKernelAltsPermission(newPath);
            return *this;
        }

        [[nodiscard]] std::filesystem::path kernelAltsPath() const {
            std::shared_lock guard{_mutex};
            return _kernelAltsPath;
        }

        NucleusPaths &setDeploymentPath(const std::filesystem::path &newPath) {
            std::unique_lock guard{_mutex};
            _deploymentPath = newPath;
            guard.unlock();
            createPath(newPath);
            _permissions.setDeploymentPermission(newPath);
            return *this;
        }

        [[nodiscard]] std::filesystem::path deploymentPath() const {
            std::shared_lock guard{_mutex};
            return _deploymentPath;
        }

        NucleusPaths &setConfigPath(const std::filesystem::path &newPath) {
            std::unique_lock guard{_mutex};
            _configPath = newPath;
            guard.unlock();
            createPath(newPath);
            _permissions.setConfigPermission(newPath);
            return *this;
        }

        [[nodiscard]] std::filesystem::path configPath() const {
            std::shared_lock guard{_mutex};
            return _configPath;
        }

        NucleusPaths &setWorkPath(const std::filesystem::path &newPath) {
            std::unique_lock guard{_mutex};
            _workPath = newPath;
            guard.unlock();
            createPath(newPath);
            _permissions.setWorkPathPermission(newPath);
            return *this;
        }

        [[nodiscard]] std::filesystem::path workPath() const {
            std::shared_lock guard{_mutex};
            return _workPath;
        }

        [[nodiscard]] std::filesystem::path rootPath() const {
            std::shared_lock guard{_mutex};
            return _rootPath;
        }

        [[nodiscard]] std::filesystem::path pluginPath() const {
            return rootPath() / PLUGINS_DIRECTORY;
        }

        NucleusPaths &setRootPath(const std::filesystem::path &newPath, bool passive = false) {
            std::unique_lock guard{_mutex};
            _rootPath = newPath;
            guard.unlock();

            if(!passive) {
                createPath(newPath);
                _permissions.setRootPermission(newPath);
            }
            return *this;
        }

        NucleusPaths &createPluginPath() {
            createPath(pluginPath());
            _permissions.setPluginPermission(pluginPath());
            return *this;
        }

        NucleusPaths &setComponentStorePath(const std::filesystem::path &newPath) {
            std::unique_lock guard{_mutex};
            _componentStorePath = newPath;
            guard.unlock();

            createPath(newPath);
            _permissions.setComponentStorePermission(newPath);
            createPath(artifactPath());
            _permissions.setComponentStorePermission(artifactPath());
            createPath(unarchivePath());
            _permissions.setComponentStorePermission(unarchivePath());
            createPath(recipePath());
            _permissions.setComponentStorePermission(recipePath());
            return *this;
        }

        [[nodiscard]] std::filesystem::path componentStorePath() const {
            std::shared_lock guard{_mutex};
            return _componentStorePath;
        }

        [[nodiscard]] std::filesystem::path artifactPath() const {
            return componentStorePath() / ARTIFACT_DIRECTORY;
        }

        //        [[nodiscard]] std::filesystem::path artifactPath(ComponentIdentifier
        //        componentIdentifier) {
        //
        //        }

        [[nodiscard]] std::filesystem::path recipePath() const {
            return componentStorePath() / RECIPE_DIRECTORY;
        }

        [[nodiscard]] std::filesystem::path unarchivePath() const {
            return componentStorePath() / ARTIFACTS_DECOMPRESSED_DIRECTORY;
        }

        //        [[nodiscard]] std::filesystem::path unarchivePath(ComponentIdentifier
        //        componentIdentifier) {
        //
        //        }

        [[nodiscard]] std::filesystem::path getBrokenDir();
        [[nodiscard]] std::filesystem::path getOldDir();
        [[nodiscard]] std::filesystem::path getNewDir();
        [[nodiscard]] std::filesystem::path getCurrentDir();
        [[nodiscard]] std::filesystem::path getInitDir();
        [[nodiscard]] std::filesystem::path getLoaderPidPath();
        [[nodiscard]] std::filesystem::path getLoaderPath();
        [[nodiscard]] std::filesystem::path getBinDir();

        [[nodiscard]] std::filesystem::path workPath(const std::string_view serviceName) {
            std::filesystem::path path{workPath() / serviceName};
            createPath(path);
            _permissions.setServiceWorkPathPermission(path);
            return path;
        }

        NucleusPaths &setTelemetryPath(const std::filesystem::path &newPath) {
            createPath(newPath);
            _permissions.setTelemetryPermission(newPath);
            return *this;
        }

        std::filesystem::path createLoggerPath(const std::filesystem::path &newPath) {
            createPath(newPath);
            _permissions.setLoggerPermission(newPath);
            return newPath;
        }

        std::filesystem::path getDefaultLoggerPath() const {
            return rootPath() / DEFAULT_LOGS_DIRECTORY;
        }
    };
} // namespace util
