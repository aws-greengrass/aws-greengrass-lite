#pragma once
#include "deployment/deployment_model.hpp"
#include "recipe_model.hpp"
#include <filesystem>

namespace package_manager {
    class RecipeLoader {

    public:
        RecipeLoader() = default;
        Recipe read(const std::filesystem::path &file);
        std::shared_ptr<data::SharedStruct> readAsStruct(const std::filesystem::path &file);
    };
} // namespace deployment
