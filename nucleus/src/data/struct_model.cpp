#include "struct_model.hpp"
#include "environment.hpp"

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
        } else if(element.isType<TrackingScope>()) {
            std::shared_ptr<data::TrackingScope> complexScope =
                element.castObject<data::TrackingScope>();
            // TODO: Need to catch cycles with TrackingScope objects
        }

        // cycleGuard may still be locked - intentional
        putAction(element);
    }

    void StructModelBase::put(std::string_view sv, const StructElement &element) {
        Handle handle = _environment.stringTable.getOrCreateOrd(sv);
        putImpl(handle, element);
    }

    void StructModelBase::put(data::StringOrd handle, const data::StructElement &element) {
        putImpl(handle, element);
    }

    bool StructModelBase::hasKey(const std::string_view sv) const {
        Handle handle = _environment.stringTable.getOrCreateOrd(sv);
        return hasKeyImpl(handle);
    }

    bool StructModelBase::hasKey(data::StringOrd handle) const {
        return hasKeyImpl(handle);
    }

    StructElement StructModelBase::get(std::string_view sv) const {
        Handle handle = _environment.stringTable.getOrCreateOrd(sv);
        return getImpl(handle);
    }

    StructElement StructModelBase::get(data::StringOrd handle) const {
        return getImpl(handle);
    }

} // namespace data
