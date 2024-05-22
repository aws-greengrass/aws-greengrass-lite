#include "package_manager.hpp"
#include "logging.hpp"
#include "recipe_model.hpp"
#include <data/shared_struct.hpp>
#include <lifecycle/kernel.hpp>

#include <temp_module.hpp>

static const auto LOG = // NOLINT(cert-err58-cpp)
    ggapi::Logger::of("com.aws.greengrass.packagemanager");

namespace package_manager {
    PackageManager::PackageManager(const scope::UsingContext &context, lifecycle::Kernel &kernel)
        : scope::UsesContext(context), _kernel(kernel) {
        _componentStore =
            std::make_shared<data::SharedQueue<std::string, deployment::Recipe>>(context);
    }

    void PackageManager::loadRecipesAndArtifacts(const deployment::Deployment &deployment) {
        auto &deploymentDocument = deployment.deploymentDocumentObj;
        if(!deploymentDocument.recipeDirectoryPath.empty()) {
            const auto &recipeDir = deploymentDocument.recipeDirectoryPath;
            copyAndLoadRecipes(recipeDir);
        }
        if(!deploymentDocument.artifactsDirectoryPath.empty()) {
            const auto &artifactsDir = deploymentDocument.artifactsDirectoryPath;
            copyArtifacts(artifactsDir);
        }
    }

    void PackageManager::copyAndLoadRecipes(const std::filesystem::path &recipeDir) {
        std::error_code ec{};
        auto iter = std::filesystem::directory_iterator(recipeDir, ec);
        if(ec != std::error_code{}) {
            LOG.atError()
                .event("recipe-load-failure")
                .kv("message", ec.message())
                .logAndThrow(std::filesystem::filesystem_error{ec.message(), recipeDir, ec});
        }

        for(const auto &entry : iter) {
            if(!entry.is_directory()) {
                deployment::Recipe recipe = loadRecipeFile(entry);
                _recipeAsStruct = loadRecipeFileAsStruct(entry);
                saveRecipeFile(recipe);
                auto semVer = recipe.componentName + "-v" + recipe.componentVersion;
                const std::hash<std::string> hasher;
                auto hashValue = hasher(semVer); // TODO: Digest hashing algorithm
                _componentStore->push({semVer, recipe});
                auto saveRecipeName =
                    std::to_string(hashValue) + "@" + recipe.componentVersion + ".recipe.yml";
                auto saveRecipeDst = _kernel.getPaths()->componentStorePath() / "recipes"
                                     / recipe.componentName / recipe.componentVersion
                                     / saveRecipeName;
                std::filesystem::copy_file(
                    entry, saveRecipeDst, std::filesystem::copy_options::overwrite_existing);
            }
        }
    }

    deployment::Recipe PackageManager::loadRecipeFile(const std::filesystem::path &recipeFile) {
        try {
            return _recipeLoader.read(recipeFile);
        } catch(...) {
            LOG.atWarn("deployment")
                .kv("DeploymentType", "LOCAL")
                .logAndThrow(std::current_exception());
        }
    }

    std::shared_ptr<data::SharedStruct> PackageManager::loadRecipeFileAsStruct(
        const std::filesystem::path &recipeFile) {
        try {
            return _recipeLoader.readAsStruct(recipeFile);
        } catch(...) {
            LOG.atWarn("deployment")
                .kv("DeploymentType", "LOCAL")
                .logAndThrow(std::current_exception());
        }
    }

    void PackageManager::saveRecipeFile(const deployment::Recipe &recipe) {
        auto saveRecipePath = _kernel.getPaths()->componentStorePath() / "recipes"
                              / recipe.componentName / recipe.componentVersion;
        if(!std::filesystem::exists(saveRecipePath)) {
            std::filesystem::create_directories(saveRecipePath);
        }
    }

    void PackageManager::copyArtifacts(std::string_view artifactsDir) {
        deployment::Recipe recipe = _componentStore->next();
        auto saveArtifactPath = _kernel.getPaths()->componentStorePath() / "artifacts"
                                / recipe.componentName / recipe.componentVersion;
        if(!std::filesystem::exists(saveArtifactPath)) {
            std::filesystem::create_directories(saveArtifactPath);
        }
        auto artifactPath =
            std::filesystem::path{artifactsDir} / recipe.componentName / recipe.componentVersion;
        std::filesystem::copy(
            artifactPath,
            saveArtifactPath,
            std::filesystem::copy_options::recursive
                | std::filesystem::copy_options::overwrite_existing);
    }
} // namespace package_manager
