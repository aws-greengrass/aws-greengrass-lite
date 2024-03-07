#include "recipe_loader.hpp"

namespace deployment {

    Recipe RecipeLoader::read(const std::filesystem::path &file) {

        Recipe recipe;
        data::Archive::readFromFile(file, recipe);

        // TODO: dependency resolution
        return recipe;
    }
} // namespace deployment
