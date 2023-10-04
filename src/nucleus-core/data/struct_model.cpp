#include "struct_model.h"
#include "environment.h"

namespace data {
    void ContainerModelBase::checkedPut(
        const StructElement &element, const std::function<void(const StructElement &)> &putAction
    ) {
        std::unique_lock cycleGuard{_environment.cycleCheckMutex, std::defer_lock};

        if(element.isContainer()) {
            std::shared_ptr<data::ContainerModelBase> otherContainer = element.getContainer();
            if(otherContainer) {
                // prepare for cycle checks
                // cycle checking requires obtaining the cycle check mutex
                // the structure mutex must be acquired after cycle check mutex
                // TODO: there has to be a better way
                cycleGuard.lock();
                otherContainer->rootsCheck(this);
            }
        }
        // cycleGuard may still be locked - intentional
        putAction(element);
    }
} // namespace data
