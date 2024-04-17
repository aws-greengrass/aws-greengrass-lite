#pragma once

#include "deployment/deployment_model.hpp"
#include "scope/context.hpp"

namespace lifecycle {
    class Kernel;
}

class TaskHandler : public scope::UsesContext {

private:
    TaskHandler *nextTaskHandler{};

protected:
    [[nodiscard]] virtual TaskHandler &getNextHandler() const {
        return *nextTaskHandler;
    }

public:
    lifecycle::Kernel &_kernel;
    TaskHandler(const TaskHandler &) = default;
    TaskHandler(TaskHandler &&) = delete;
    TaskHandler &operator=(const TaskHandler &) = delete;
    TaskHandler &operator=(TaskHandler &&) = delete;
    TaskHandler(const scope::UsingContext &context, lifecycle::Kernel &kernel)
        : scope::UsesContext{context}, _kernel(kernel){};
    virtual ~TaskHandler() = default;
    virtual deployment::DeploymentResult handleRequest(deployment::Deployment &deployment) = 0;

    virtual void setNextHandler(TaskHandler &handler) {
        nextTaskHandler = &handler;
    }
};
