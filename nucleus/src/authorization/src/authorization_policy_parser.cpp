#include "lifecycle/kernel.hpp"
#include <authorization_policy.hpp>

namespace authorization {
    AuthorizationPolicyParser::AuthorizationPolicyParser(
        const scope::UsingContext &context, lifecycle::Kernel &kernel)
        : scope::UsesContext(context), _kernel(kernel) {
    }

    std::unordered_map<std::string, std::list<AuthorizationPolicy>>
    AuthorizationPolicyParser::parseAllAuthorizationPolicies() {
        std::unordered_map<std::string, std::list<AuthorizationPolicy>>
            _primaryAuthorizationPolicyMap;

        discoverRecipes(_kernel.getPaths()->componentStorePath() / "recipes");

        return _primaryAuthorizationPolicyMap;
    }

    std::unordered_map<std::string, std::list<AuthorizationPolicy>>
    AuthorizationPolicyParser::parseAllPoliciesForComponent(std::string sourceComponent) {
    }

    std::list<AuthorizationPolicy> AuthorizationPolicyParser::parseAuthorizationPolicyConfig(
        std::string componentName,
        std::unordered_map<std::string, AuthorizationPolicyConfig> accessControlConfig) {
    }

    // TODO: Remove recipes discovering? move funct to lookuptopics on services namespace
    // SERVICES_TOPIC_KEY
    void AuthorizationPolicyParser::discoverRecipes(std::filesystem::path csRecipeDir) {
        // recipes in the component store will always follow this path directory schematic
        // {componentStoreRecipesPath}/componentName/componentVersion/recipeName.yml~json
        for(const auto &componentDir : std::filesystem::directory_iterator(csRecipeDir)) {
            // cycle through all the components
            if(componentDir.is_directory()) {
                auto componentName = componentDir.path().filename().string();
                // cycle through all the versions of the component
                for(const auto &versionDir : std::filesystem::directory_iterator(componentDir)) {
                    if(versionDir.is_directory()) {
                        auto componentVersion = versionDir.path().filename().string();
                        // cycle through for all recipes (should be only 1)
                        for(const auto &recipeFile :
                            std::filesystem::directory_iterator(versionDir)) {
                            if(recipeFile.is_regular_file()) {
                                discoverRecipe(componentName, recipeFile);
                            }
                        }
                    }
                }
            }
        }
    }

    void AuthorizationPolicyParser::discoverRecipe(
        std::string componentName, const std::filesystem::directory_entry &entry) {
        std::string ext = util::lower(entry.path().extension().generic_string());
        // For every recipe found, add to the recipePaths unordered map
        if(ext == ".yaml" || ext == ".yml" || ext == ".json") {
            _recipePaths.emplace(componentName, entry.path());
        }
    }
} // namespace authorization
