#include "deployment_queue.hpp"

bool shouldReplaceAnExistingDeployment(const deployment::Deployment &newDeployment, const deployment::Deployment &existingDeployment) {
    // If the enqueued deployment is already in progress (Non DEFAULT state), then it can not be replaced.
   if (deployment::DeploymentStage::DEFAULT != existingDeployment.deploymentStage){
       return false;
   }

   // If the enqueued deployment is of type SHADOW, then replace it.
   // If the offered deployment is cancelled, then replace the enqueued with the offered one
   if (deployment::DeploymentType::SHADOW == newDeployment.deploymentType || newDeployment.isCancelled) {
       return true;
   }

   // If the offered deployment is in non DEFAULT stage, then replace the enqueued with the offered one.
   return deployment::DeploymentStage::DEFAULT != newDeployment.deploymentStage;
};

bool DeploymentQueue::offer(const deployment::Deployment &newDeployment) {
    auto _deployment_id = newDeployment.id;
    if (!_shared_ordered_map->contains(_deployment_id)) {
        _shared_ordered_map->push({_deployment_id, newDeployment});
        return true;
    }
    // If the queue already contains a deployment with the same id, then replace it if the criteria is satisfied.
    // Ignore it otherwise.
    auto existingDeployment = _shared_ordered_map->get(_deployment_id);
    if(shouldReplaceAnExistingDeployment(newDeployment, existingDeployment)) {
        _shared_ordered_map->push({_deployment_id, newDeployment});
        return true;
    }
    return false;
};

deployment::Deployment DeploymentQueue::poll() {
    return _shared_ordered_map->poll(); // returns and removes entry from the queue
};

void DeploymentQueue::clear() {
    _shared_ordered_map->clear();
}

long DeploymentQueue::size() {
   return _shared_ordered_map->size();
}
