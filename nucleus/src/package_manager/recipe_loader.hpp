#pragma once
#include <filesystem>

namespace data {
    class SharedStruct;
}
namespace deployment {
    class Recipe;
}

namespace package_manager {
    class RecipeLoader {
    public:
        deployment::Recipe read(const std::filesystem::path &file);
        std::shared_ptr<data::SharedStruct> readAsStruct(const std::filesystem::path &file);
    };
} // namespace package_manager
