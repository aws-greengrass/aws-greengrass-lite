#include "nucleus_paths.hpp"
#include <util.hpp>

namespace util {

    void NucleusPaths::createPath(const std::filesystem::path & path) { // NOLINT(*-no-recursion)
        if (std::filesystem::exists(path)) {
            return;
        }
        if (path.has_parent_path()) {
            std::filesystem::path parent = path.parent_path();
            if (parent != path) {
                createPath(parent); // recursively apply
            }
        }
        std::filesystem::create_directory(path);
        std::filesystem::permissions(path,
                                     std::filesystem::perms::group_write | std::filesystem::perms::others_write,
                                     std::filesystem::perm_options::remove);
    }

    std::filesystem::path NucleusPaths::deTilde(std::string_view s) const {
        std::shared_lock guard{_mutex};
        // converts a string-form path into an OS path
        // replicates GG-Java
        // TODO: code tracks GG-Java, noting that '/' and '\\' should be interchangable for Windows
        if (util::startsWith(s, HOME_DIR_PREFIX)) {
            return resolve(_homePath, util::trimStart(s, HOME_DIR_PREFIX));
        }
        if (!_rootPath.empty() && util::startsWith(s, ROOT_DIR_PREFIX)) {
            return resolve(_rootPath, util::trimStart(s, ROOT_DIR_PREFIX));
        }
        if (!_configPath.empty() && util::startsWith(s, CONFIG_DIR_PREFIX)) {
            return resolve(_configPath, util::trimStart(s, CONFIG_DIR_PREFIX));
        }
        if (!_componentStorePath.empty() && util::startsWith(s, PACKAGE_DIR_PREFIX)) {
            return resolve(_componentStorePath, util::trimStart(s, PACKAGE_DIR_PREFIX));
        }
        return resolve(".", s);
    }

    void NucleusPaths::initPaths(std::string_view rootPathString) {
        std::filesystem::path rootPath {std::filesystem::absolute(std::filesystem::path(rootPathString))};
        setRootPath(rootPath);
        createPluginPath();
        // TODO: Telementry
        // TODO: LogManager
        setWorkPath(rootPath/WORK_PATH_NAME);
        setComponentStorePath(rootPath/PACKAGES_PATH_NAME);
        setConfigPath(rootPath/CONFIG_PATH_NAME);
        setKernelAltsPath(rootPath/ALTS_PATH_NAME);
        setDeploymentPath(rootPath/DEPLOYMENTS_PATH_NAME);
        setCliIpcInfoPath(rootPath/CLI_IPC_INFO_PATH_NAME);
        setBinPath(rootPath/BIN_PATH_NAME);

        // TODO: re-initialize deployment manager
    }

    std::filesystem::path NucleusPaths::resolve(const std::filesystem::path & first, const std::filesystem::path & second) {
        if (second.is_absolute()) {
            return second;
        } else {
            return std::filesystem::absolute(first / second);
        }
    }

    std::filesystem::path NucleusPaths::resolve(const std::filesystem::path & first, std::string_view second) {
        return resolve(first, std::filesystem::path(second));
    }


}
