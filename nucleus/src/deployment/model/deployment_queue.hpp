#include "deployment/model/linked_map.hpp"
#include "deployment/deployment_model.hpp"
#include <iostream>
#include <memory>


class DeploymentQueue  {
private:
    using OrderedMap = data::LinkedMap<std::string, deployment::Deployment>;
    std::unique_ptr<OrderedMap> _shared_ordered_map{std::make_unique<OrderedMap>()};

public:
    bool offer(const deployment::Deployment &deployment);
    deployment::Deployment poll();
    void clear();
    long size();
};
