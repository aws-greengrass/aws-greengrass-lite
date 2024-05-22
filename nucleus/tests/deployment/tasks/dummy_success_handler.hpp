#include "deployment/task/default_deployment_task.hpp"

// For testing, instead of passing to the next handler, pass to a dummy handler that will return success state
class DummySuccessHandler : public TaskHandler  {
public:
    DummySuccessHandler(const scope::UsingContext &context, lifecycle::Kernel &kernel)
            : TaskHandler(context,kernel) {
    }
    deployment::DeploymentResult handleRequest(deployment::Deployment& deployment) override {
        return deployment::DeploymentResult{deployment::DeploymentStatus::SUCCESSFUL};
    }
};
