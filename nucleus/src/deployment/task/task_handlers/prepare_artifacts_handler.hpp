#include "task_handler.hpp"

class PrepareArtifactsHandler: public TaskHandler  {
public:
    PrepareArtifactsHandler(const scope::UsingContext &context, lifecycle::Kernel &kernel)
        : TaskHandler(context,kernel) {
    }
    deployment::DeploymentResult handleRequest(deployment::Deployment& deployment) override {
        // Before downloading, see if the component recipe exists, if not exit.
        // Now check if artifact pre-req exists, otherwise, exit.
        // Use s3 client to download artifacts.
        // Change directory permissions.

        return {};
    }
};
