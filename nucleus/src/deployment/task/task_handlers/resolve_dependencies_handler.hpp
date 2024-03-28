#include "task_handler.hpp"

class ResolveDependenciesHandler: public TaskHandler {
public:
    ResolveDependenciesHandler(const scope::UsingContext &context, lifecycle::Kernel &kernel)
        : TaskHandler(context,kernel) {
    }
    deployment::DeploymentResult handleRequest(deployment::Deployment& deployment) override {
        return {};
    }
};
