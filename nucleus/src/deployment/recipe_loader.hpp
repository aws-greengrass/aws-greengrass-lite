#pragma once
#include "deployment_model.hpp"
#include "plugin.hpp"
#include "util.hpp"
#include <filesystem>

namespace deployment {
    enum class OS { ALL, WINDOWS, LINUX, DARWIN, MACOS, UNKNOWN };

    static constexpr std::string_view ALL{"all"};
    static constexpr std::string_view WINDOWS{"windows"};
    static constexpr std::string_view LINUX{"linux"};
    static constexpr std::string_view DARWIN{"darwin"};
    static constexpr std::string_view MACOS{"macos"};
    static constexpr std::string_view UNKNOWN{"unknown"};

    inline static const util::LookupTable<std::string_view, OS, 6> OSMap{
        ALL,
        OS::ALL,
        WINDOWS,
        OS::WINDOWS,
        LINUX,
        OS::LINUX,
        DARWIN,
        OS::DARWIN,
        MACOS,
        OS::MACOS,
        UNKNOWN,
        OS::UNKNOWN,
    };

    enum class Architecture { ALL, AMD64, ARM, AARCH64, x86, UNKNOWN };

    static constexpr std::string_view AMD64{"amd64"};
    static constexpr std::string_view ARM{"arm"};
    static constexpr std::string_view AARCH64{"aarch64"};
    static constexpr std::string_view x86{"x86"};

    inline static const util::LookupTable<std::string_view, Architecture, 5> ArchitectureMap{
        ALL,
        Architecture::ALL,
        AMD64,
        Architecture::AMD64,
        ARM,
        Architecture::ARM,
        AARCH64,
        Architecture::AARCH64,
        x86,
        Architecture::x86};

    enum class Unarchive {
        NONE,
        ZIP,
    };

    static constexpr std::string_view NONE{"NONE"};
    static constexpr std::string_view ZIP{"ZIP"};

    inline static const util::LookupTable<std::string_view, Unarchive, 2> UnarchiveMap{
        NONE, Unarchive::NONE, ZIP, Unarchive::ZIP};

    enum class PermissionType {
        NONE,
        OWNER,
        ALL,
    };

    static constexpr std::string_view OWNER{"OWNER"};

    inline static const util::LookupTable<std::string_view, PermissionType, 3> PermissionMap{
        NONE, PermissionType::NONE, OWNER, PermissionType::OWNER, ALL, PermissionType::ALL};

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

    struct ComponentArtifact {
        std::string uri;
        std::string digest;
        std::string algorithm;
        Unarchive unarchive;
        Permission permission;
    };

    enum class DependencyType : uint32_t { HARD, SOFT };

    static constexpr std::string_view HARD{"HARD"};
    static constexpr std::string_view SOFT{"SOFT"};

    inline static const util::LookupTable<std::string_view, DependencyType, 2> DependencyTypeMap{
        HARD,
        DependencyType::HARD,
        SOFT,
        DependencyType::SOFT,
    };

    struct DependencyProperties {
        std::string versionRequirement;
        DependencyType dependencyType = DependencyType::HARD;
    };

    struct ComponentConfiguration {
        ggapi::Struct defaultConfiguration;
    };

    struct Platform {
        OS os;
        Architecture architecture;
    };

    struct Command {
        bool requiresPrivilege = false;
        std::string skipIf;
        std::string script;
        int timeout;
        std::unordered_map<std::string, std::string> envs;
    };

    struct PlatformManifest {
        Platform platform;
        std::string name;
        std::forward_list<std::tuple<std::string, Command>> lifecycle;
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
        std::forward_list<std::tuple<std::string, Command>> lifecycle;

        void setFormatVersion(std::string_view version) {
            formatVersion = version;
        }

        [[nodiscard]] std::string getFormatVersion() const {
            return formatVersion;
        }
    };

    class RecipeLoader {
        void loadMetadata(const ggapi::Struct &data, Recipe &recipe);
        void loadConfiguration(const ggapi::Struct &data, Recipe &recipe);
        void loadDependencies(const ggapi::Struct &data, Recipe &recipe);
        void loadManifests(const ggapi::Struct &data, Recipe &recipe);
        void loadLifecycle(const ggapi::Struct &data, PlatformManifest &manifest);
        void loadArtifact(const ggapi::Struct &data, PlatformManifest &recipe);
        void loadCommand(
            std::string_view stepName, const ggapi::Struct &data, PlatformManifest &manifest);

    public:
        RecipeLoader() = default;
        Recipe read(const std::filesystem::path &file);
    };
} // namespace deployment
