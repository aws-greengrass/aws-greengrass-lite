#pragma once
#include "config/yaml_deserializer.hpp"
#include "data/object_model.hpp"
#include "data/serializable.hpp"
#include "data/shared_struct.hpp"

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

    struct Permission : conv::Serializable {
        std::string read;
        std::string execute;

        void serialize(config::YamlDeserializer &archive) {
            archive("Read", read);
            archive("Execute", execute);
        }
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

    struct ComponentArtifact : conv::Serializable {
        std::string uri;
        std::string digest;
        std::string algorithm;
        std::string unarchive;
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

        [[nodiscard]] Permission getPermission() const {
            return permission;
        }

        void serialize(config::YamlDeserializer &archive) {
            archive("URI", uri);
            archive("Unarchive", unarchive);
            archive("Permission", permission);
            archive("Digest", digest);
            archive("Algorithm", algorithm);
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

    struct DependencyProperties : conv::Serializable {
        std::string versionRequirement;
        std::string dependencyType;

        [[nodiscard]] std::string getVersionRequirement() {
            return versionRequirement;
        }

        void serialize(config::YamlDeserializer &archive) {
            archive("VersionRequirement", versionRequirement);
            archive("DependencyType", dependencyType);
        }
    };

    struct ComponentConfiguration : conv::Serializable {
        std::shared_ptr<data::SharedStruct> defaultConfiguration;

        void serialize(config::YamlDeserializer &archive) {
            archive(defaultConfiguration);
        }
    };

    struct Platform : conv::Serializable {
        std::string os;
        std::string architecture;
        std::string nucleusType;

        void serialize(config::YamlDeserializer &archive) {
            archive("os", os);
            archive("architecture", architecture);
            archive("nucleus", nucleusType);
        }
    };

    struct PlatformManifest : conv::Serializable {
        std::string name;
        Platform platform;
        std::unordered_map<std::string, data::Object> lifecycle;
        std::vector<std::string> selections;
        std::vector<ComponentArtifact> artifacts;

        void serialize(config::YamlDeserializer &archive) {
            archive("Name", name);
            archive("Platform", platform);
            archive("Lifecycle", lifecycle);
            archive("Selections", selections);
            archive("Artifacts", artifacts);
        }
    };

    struct Recipe : public conv::Serializable {
        std::string formatVersion;
        std::string componentName;
        std::string componentVersion;
        std::string componentDescription;
        std::string componentPublisher;
        ComponentConfiguration configuration;
        std::unordered_map<std::string, DependencyProperties> componentDependencies;
        std::string componentType;
        std::string componentSource;
        std::vector<PlatformManifest> manifests;
        std::unordered_map<std::string, data::Object> lifecycle;

        void serialize(config::YamlDeserializer &archive) {
            archive("RecipeFormatVersion", formatVersion);
            archive("ComponentName", componentName);
            archive("ComponentVersion", componentVersion);
            archive("ComponentDescription", componentDescription);
            archive("ComponentPublisher", componentPublisher);
            archive("ComponentConfiguration", configuration);
            archive("ComponentDependencies", componentDependencies);
            archive("ComponentType", componentType);
            archive("ComponentSource", componentSource);
            archive("Manifests", manifests);
            archive("Lifecycle", lifecycle);
        }

        [[nodiscard]] std::string getFormatVersion() const {
            return formatVersion;
        }

        [[nodiscard]] std::string getComponentName() const {
            return componentName;
        };

        [[nodiscard]] std::string getComponentVersion() const {
            return componentVersion;
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

        [[nodiscard]] std::unordered_map<std::string, DependencyProperties>
        getComponentDependencies() const {
            return componentDependencies;
        }

        [[nodiscard]] std::vector<PlatformManifest> getManifests() const {
            return manifests;
        }
    };
} // namespace deployment
