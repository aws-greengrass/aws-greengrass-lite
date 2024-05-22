#pragma once
#include "../data/shared_struct.hpp"
#include "../data/shared_queue.hpp"
#include "../deployment/deployment_model.hpp"
#include "../scope/context.hpp"
#include "recipe_loader.hpp"
#include <memory>

namespace data {
    template<class K, class V>
    class SharedQueue;
}

namespace lifecycle {
    class Kernel;
}

//namespace deployment {
//    template<class K, class V>
//    using DeploymentQueue = std::shared_ptr<data::SharedQueue<K, V>>;
//}

namespace package_manager {

    template<class K, class V>
    using DeploymentQueue = std::shared_ptr<data::SharedQueue<K, V>>;

    class PackageManager : private scope::UsesContext {
        lifecycle::Kernel &_kernel;
        RecipeLoader _recipeLoader;

    public:
        explicit PackageManager(const scope::UsingContext &, lifecycle::Kernel &kernel);
        void loadRecipesAndArtifacts(const deployment::Deployment &);
        void copyAndLoadRecipes(const std::filesystem::path &);
        deployment::Recipe loadRecipeFile(const std::filesystem::path &);
        std::shared_ptr<data::SharedStruct> loadRecipeFileAsStruct(const std::filesystem::path &);
        void saveRecipeFile(const deployment::Recipe &);
        void copyArtifacts(std::string_view);

        std::shared_ptr<data::SharedStruct> _recipeAsStruct;
        DeploymentQueue<std::string, deployment::Recipe> _componentStore;
    };
}
