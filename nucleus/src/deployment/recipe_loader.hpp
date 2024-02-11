#pragma once
#include "deployment_model.hpp"
#include "recipe_model.hpp"
#include "plugin.hpp"
#include "util.hpp"
#include <filesystem>

namespace deployment {
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
