#pragma once
#include <filesystem>

namespace util {
    class Permissions {
    public:
        void setComponentStorePermission(const std::filesystem::path &newPath) {
        }

        void setArtifactStorePermission(const std::filesystem::path &newPath) {
        }

        void setRecipeStorePermission(const std::filesystem::path &newPath) {
        }

        void setWorkPathPermission(const std::filesystem::path &newPath) {
        }

        void setServiceWorkPathPermission(const std::filesystem::path &newPath) {
        }

        void setRootPermission(const std::filesystem::path &newPath) {
        }

        void setKernelAltsPermission(const std::filesystem::path &newPath) {
        }

        void setDeploymentPermission(const std::filesystem::path &newPath) {
        }

        void setConfigPermission(const std::filesystem::path &newPath) {
        }

        void setPluginPermission(const std::filesystem::path &newPath) {
        }

        void setTelemetryPermission(const std::filesystem::path &newPath) {
        }

        void setLoggerPermission(const std::filesystem::path &newPath) {
        }

        void setCliIpcInfoPermission(const std::filesystem::path &newPath) {
        }

        void setBinPermission(const std::filesystem::path &newPath) {
        }

        void setPrivateKeyPermission(const std::filesystem::path &newPath) {
        }
    };
} // namespace util
