#include "task_handler.hpp"

class KernelConfigResolveHandler : public TaskHandler  {
public:
    KernelConfigResolveHandler(const scope::UsingContext &context, lifecycle::Kernel &kernel)
        : TaskHandler(context,kernel) {
    }
    deployment::DeploymentResult handleRequest(deployment::Deployment& deployment) override {
        return {};
    }
};

