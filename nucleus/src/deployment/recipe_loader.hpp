#pragma once
#include "deployment_model.hpp"
#include "plugin.hpp"
#include "util.hpp"
#include <filesystem>

namespace deployment {
    enum class NucleusType {
        JAVA,
        LITE,
        UNKNOWN,
    };

    inline static const util::LookupTable<std::string_view, NucleusType, 2> NucleusMap{
        "java",
        NucleusType::JAVA,
        "lite",
        NucleusType::LITE,
    };

    enum class OS { ALL, WINDOWS, LINUX, DARWIN, MACOS, UNKNOWN };

    inline static const util::LookupTable<std::string_view, OS, 6> OSMap{
        "all",
        OS::ALL,
        "windows",
        OS::WINDOWS,
        "linux",
        OS::LINUX,
        "darwin",
        OS::DARWIN,
        "macos",
        OS::MACOS,
        "unknown",
        OS::UNKNOWN,
    };

    enum class Architecture { ALL, AMD64, ARM, AARCH64, x86, UNKNOWN };

    inline static const util::LookupTable<std::string_view, Architecture, 5> ArchitectureMap{
        "all",
        Architecture::ALL,
        "amd64",
        Architecture::AMD64,
        "arm",
        Architecture::ARM,
        "aarch64",
        Architecture::AARCH64,
        "x86",
        Architecture::x86};

    enum class Unarchive {
        NONE,
        ZIP,
    };

    inline static const util::LookupTable<std::string_view, Unarchive, 2> UnarchiveMap{
        "NONE", Unarchive::NONE, "ZIP", Unarchive::ZIP};

    enum class PermissionType {
        NONE,
        OWNER,
        ALL,
    };

    inline static const util::LookupTable<std::string_view, PermissionType, 3> PermissionMap{
        "NONE", PermissionType::NONE, "OWNER", PermissionType::OWNER, "ALL", PermissionType::ALL};

    struct Permission {
        PermissionType read = PermissionType::OWNER;
        PermissionType execute = PermissionType::NONE;
    };

    enum class ComponentType {
        GENERIC,
        LAMBDA,
        PLUGIN,
        NUCLEUS,
    };

    inline static const util::LookupTable<std::string_view, ComponentType, 4> ComponentTypeMap{
        "aws.greengrass.generic",
        ComponentType::GENERIC,
        "aws.greengrass.lambda",
        ComponentType::LAMBDA,
        "aws.greengrass.plugin",
        ComponentType::PLUGIN,
        "aws.greengrass.nucleus",
        ComponentType::NUCLEUS,
    };

    struct ComponentArtifact {
        std::string uri;
        std::string digest;
        std::string algorithm;
        Unarchive unarchive;
        Permission permission;

        [[nodiscard]] std::string getURI() const {
            return uri;
        }

        [[nodiscard]] std::string getDigest() const {
            return digest;
        }

        [[nodiscard]] std::string getAlgorithm() const {
            return algorithm;
        }

        [[nodiscard]] Unarchive getUnArchive() const {
            return unarchive;
        }

        [[nodiscard]] Permission getPermission() const {
            return permission;
        }
    };

    enum class DependencyType : uint32_t { HARD, SOFT };

    inline static const util::LookupTable<std::string_view, DependencyType, 2> DependencyTypeMap{
        "HARD",
        DependencyType::HARD,
        "SOFT",
        DependencyType::SOFT,
    };

    static constexpr std::string_view on_path_prefix = "onpath";
    static constexpr std::string_view exists_prefix = "exists";

    enum class LifecycleStep {
        INSTALL,
        RUN,
        STARTUP,
        SHUTDOWN,
        RECOVER,
    };

    inline static const util::LookupTable<std::string_view, LifecycleStep, 5> LifecycleStepMap{
        "install",
        LifecycleStep::INSTALL,
        "run",
        LifecycleStep::RUN,
        "startup",
        LifecycleStep::STARTUP,
        "shutdown",
        LifecycleStep::SHUTDOWN,
        "recover",
        LifecycleStep::RECOVER,
    };

    struct DependencyProperties {
        std::string versionRequirement;
        DependencyType dependencyType = DependencyType::HARD;

        [[nodiscard]] std::string getVersionRequirement() {
            return versionRequirement;
        }

        [[nodiscard]] DependencyType getDependencyType() {
            return dependencyType;
        }
    };

    struct ComponentConfiguration {
        ggapi::Struct defaultConfiguration;
    };

    struct Platform {
        OS os;
        Architecture architecture;
        NucleusType nucleusType;

        [[nodiscard]] OS getOS() const {
            return os;
        }

        [[nodiscard]] Architecture getArchitecture() {
            return architecture;
        }

        [[nodiscard]] NucleusType getNucleusType() {
            return nucleusType;
        }
    };

    struct Command {
        bool requiresPrivilege = false;
        std::string skipIf;
        std::string script;
        int timeout = 15;
        std::unordered_map<std::string, std::string> environment;

        [[nodiscard]] bool getRequiresPrivilege() const {
            return requiresPrivilege;
        }

        [[nodiscard]] std::string getSkipIf() const {
            return skipIf;
        }

        [[nodiscard]] std::string getScript() const {
            return script;
        }

        [[nodiscard]] int getTimeout() const {
            return timeout;
        }

        [[nodiscard]] std::unordered_map<std::string, std::string> getEnvironment() {
            return environment;
        }
    };

    using lifecycleStep = std::pair<std::string, Command>;

    struct Lifecycle {
        std::unordered_map<std::string, std::string> environment;
        std::forward_list<lifecycleStep> steps;

        std::unordered_map<std::string, std::string> getEnvironment() {
            return environment;
        }
    };

    struct PlatformManifest {
        Platform platform;
        std::string name;
        Lifecycle lifecycle;
        std::vector<ComponentArtifact> artifacts;
        std::vector<std::string> selections;
        std::unordered_map<std::string, std::string> envs;
    };

    struct Recipe {
        std::string formatVersion;
        std::string componentName;
        std::string componentVersion;
        ComponentType componentType = ComponentType::GENERIC;
        std::string componentDescription;
        std::string componentPublisher;
        std::string componentSource;
        ComponentConfiguration configuration;
        std::vector<ComponentArtifact> artifacts;
        std::unordered_map<std::string, DependencyProperties> componentDependencies;
        std::vector<PlatformManifest> manifests;
        std::unordered_map<std::string, Lifecycle> lifecycle;

        [[nodiscard]] std::string getFormatVersion() const {
            return formatVersion;
        }

        [[nodiscard]] std::string getComponentName() const {
            return componentName;
        };

        [[nodiscard]] std::string getComponentVersion() const {
            return componentVersion;
        }

        [[nodiscard]] ComponentType getComponentType() const {
            return componentType;
        }

        [[nodiscard]] std::string getComponentDescription() const {
            return componentDescription;
        }

        [[nodiscard]] std::string getComponentPublisher() const {
            return componentPublisher;
        }

        [[nodiscard]] std::string getComponentSource() const {
            return componentSource;
        }

        [[nodiscard]] ComponentConfiguration getComponentConfiguration() const {
            return configuration;
        }

        [[nodiscard]] std::vector<ComponentArtifact> getArtifacts() const {
            return artifacts;
        }

        [[nodiscard]] std::unordered_map<std::string, DependencyProperties>
        getComponentDependencies() const {
            return componentDependencies;
        }

        [[nodiscard]] std::vector<PlatformManifest> getManifests() const {
            return manifests;
        }

        [[nodiscard]] std::unordered_map<std::string, Lifecycle> getLifecycle() const {
            return lifecycle;
        };
    };

    class RecipeLoader {
        void loadMetadata(const ggapi::Struct &data, Recipe &recipe);
        void loadConfiguration(const ggapi::Struct &data, Recipe &recipe);
        void loadDependencies(const ggapi::Struct &data, Recipe &recipe);
        void loadManifests(const ggapi::Struct &data, Recipe &recipe);
        void loadGlobalLifecycle(const ggapi::Struct &data, Recipe &recipe);
        Lifecycle loadLifecycle(const ggapi::Struct &data);
        void loadArtifact(const ggapi::Struct &data, PlatformManifest &recipe);
        Command loadCommand(std::string_view stepName, const ggapi::Struct &data);

    public:
        RecipeLoader() = default;
        Recipe read(const std::filesystem::path &file);
    };
} // namespace deployment
