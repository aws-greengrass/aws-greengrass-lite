#pragma once
#include "deployment_model.hpp"
#include "plugin.hpp"
#include "recipe_model.hpp"
#include "util.hpp"
#include <filesystem>

namespace deployment {
    class RecipeLoader {

    public:
        RecipeLoader() = default;
        Recipe read(const std::filesystem::path &file);
    };
} // namespace deployment
