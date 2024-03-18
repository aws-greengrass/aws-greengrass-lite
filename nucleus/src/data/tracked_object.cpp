#include "tracked_object.hpp"
#include "scope/context_full.hpp"
#include "tasks/task.hpp"

namespace data {

    TrackingScope::TrackingScope(const scope::UsingContext &context)
        : TrackedObject(context), _root(context.newRootHandle()) {
    }

    bool RootHandle::release() noexcept {
        if(*this) {
            return table().releaseRoot(*this);
        } else {
            return true;
        }
    }

    bool ObjHandle::release() noexcept {
        if(*this) {
            return table().release(*this);
        } else {
            return true;
        }
    }

    std::shared_ptr<TrackedObject> ObjHandle::toObjectHelper() const {
        if(*this) {
            return table().get(*this);
        } else {
            return {};
        }
    }
} // namespace data
