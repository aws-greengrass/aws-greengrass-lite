#include "task_handler.hpp"

class TrackServiceStateHandler : public TaskHandler{
public:
    TrackServiceStateHandler(const scope::UsingContext &context, lifecycle::Kernel &kernel)
        : TaskHandler(context,kernel) {
    }
    deployment::DeploymentResult handleRequest(deployment::Deployment& deployment) override {
        return {};
    }
};

