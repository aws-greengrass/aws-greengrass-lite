#include "recipe_loader.hpp"
#include "data/generic_serializer.hpp"
#include "scope/context_full.hpp"
#include <memory>

namespace deployment {

    Recipe RecipeLoader::read(const std::filesystem::path &file) {

        Recipe recipe;
        data::archive::readFromFile(file, recipe);

        // TODO: dependency resolution
        return recipe;
    }

    std::shared_ptr<data::SharedStruct> RecipeLoader::readAsStruct(const std::filesystem::path &file) {
        auto recipe = std::make_shared<data::SharedStruct>(scope::context());
        data::ArchiveExtend::readFromFileStruct(file, recipe);
        return recipe;
    }
} // namespace deployment
