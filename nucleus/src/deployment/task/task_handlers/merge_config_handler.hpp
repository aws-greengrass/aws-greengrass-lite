#include "task_handler.hpp"


class MergeConfigHandler : public TaskHandler  {
public:
    MergeConfigHandler(const scope::UsingContext &context, lifecycle::Kernel &kernel)
        : TaskHandler(context,kernel) {
    }
    deployment::DeploymentResult handleRequest(deployment::Deployment& deployment) override {
        return {};
    }
};

