#include "recipe_loader.hpp"
#include "config/yaml_recipe.hpp"
//#include "config/json_recipe.hpp"
#include <fstream>

namespace deployment {

    Recipe RecipeLoader::read(const std::filesystem::path &file) {
        std::ifstream stream{file};
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        if(!stream.is_open()) {
            throw std::runtime_error("Unable to read config file");
        }

        Recipe recipe;

        std::string ext = util::lower(file.extension().generic_string());
        // TODO: Json support
        if(ext == ".yaml" || ext == ".yml") {
            config::YamlRecipeReader yamlRecipeReader(scope::context());
            yamlRecipeReader.read(file);
            yamlRecipeReader(recipe);
        } else if(ext == ".json") {
            throw std::runtime_error("Unsupported recipe file type");
        } else {
            throw std::runtime_error("Unsupported recipe file type");
        }

        // TODO: dependency resolution
        return recipe;
    }
} // namespace deployment
