#include "task_handler.hpp"

class PrepareArtifactsHandler: public TaskHandler  {
public:
    PrepareArtifactsHandler(const scope::UsingContext &context, lifecycle::Kernel &kernel)
        : TaskHandler(context,kernel) {
    }
    deployment::DeploymentResult handleRequest(deployment::Deployment& deployment) override {
        return {};
    }
};

