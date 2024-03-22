#pragma once
#include "deployment/deployment_model.hpp"
#include "scope/context_full.hpp"

class TaskHandler: public scope::UsesContext {

private:
     TaskHandler* nextTaskHandler{};

protected:
    [[nodiscard]] virtual TaskHandler &getNextHandler() const {
        return *nextTaskHandler;
    }

public:
    lifecycle::Kernel &_kernel;
    TaskHandler(const scope::UsingContext &context, lifecycle::Kernel &kernel)
        : scope::UsesContext{context}, _kernel(kernel) {};
    virtual ~TaskHandler() = default;
    virtual deployment::DeploymentResult handleRequest(deployment::Deployment &deployment){};

    virtual void setNextHandler(TaskHandler& handler) {
        nextTaskHandler = &handler;
    }


};

