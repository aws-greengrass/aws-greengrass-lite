#include "recipe_loader.hpp"
#include "data/generic_serializer.hpp"
#include "package_manager/recipe_model.hpp"

namespace package_manager {
    deployment::Recipe RecipeLoader::read(const std::filesystem::path &file) {

        deployment::Recipe recipe;
        data::archive::readFromFile(file, recipe);

        // TODO: dependency resolution
        return recipe;
    }

    std::shared_ptr<data::SharedStruct> package_manager::RecipeLoader::readAsStruct(
        const std::filesystem::path &file) {
        auto recipe = std::make_shared<data::SharedStruct>(scope::context());
        data::ArchiveExtend::readFromFileStruct(file, recipe);
        return recipe;
    }
} // namespace package_manager
