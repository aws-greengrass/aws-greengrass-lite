#include "tracked_object.hpp"
#include "scope/context_full.hpp"
#include "tasks/task.hpp"

namespace data {
    ObjectAnchor TrackingRoot::anchor(const std::shared_ptr<TrackedObject> &obj) {
        if(!obj) {
            return {};
        }
        return context().handles().create(ObjectAnchor{obj, baseRef()});
    }

    ObjectAnchor TrackingRoot::createRootHelper(const data::ObjectAnchor &anchor) {
        // assume handleTable may be locked on entry, beware of recursive locks
        std::unique_lock guard{_mutex};
        _roots.emplace(context().handles().partial(anchor.getHandle()), anchor.getBase());
        return anchor;
    }

    void TrackingRoot::remove(const data::ObjectAnchor &anchor) {
        context().handles().remove(anchor);
    }

    void ObjectAnchor::release() {
        std::shared_ptr<TrackingRoot> root{_root.lock()};
        if(root) {
            // go via root - root has access to context()
            root->remove(*this);
        } else {
            // if owner has gone away, handle will be deleted
            // so nothing to do here
        }
    }

    void TrackingRoot::removeRootHelper(const data::ObjectAnchor &anchor) {
        // always called from HandleTable
        // assume handleTable could be locked on entry, beware of recursive locks
        auto p = context().handles().partial(anchor);
        std::unique_lock guard{_mutex};
        _roots.erase(p);
    }

    std::vector<ObjectAnchor> TrackingRoot::getRootsHelper(
        const std::weak_ptr<TrackingRoot> &assumedOwner) {
        auto ctx = _context.lock();
        if(!ctx) {
            return {}; // context shutting down, short circuit
        }
        std::shared_lock guard{_mutex};
        std::vector<ObjectAnchor> copy;
        for(const auto &i : _roots) {
            ObjectAnchor anc{i.second, assumedOwner};
            copy.push_back(anc.withHandle(ctx->handles().apply(i.first)));
        }
        return copy;
    }

    TrackingRoot::~TrackingRoot() {
        if(_context.expired()) {
            return; // cleanup not required if context destroyed
        }
        for(const auto &i : getRootsHelper({})) {
            remove(i);
        }
    }

    TrackingScope::~TrackingScope() {
        _root.reset();
    }

    TrackingScope::TrackingScope(const std::shared_ptr<scope::Context> &context)
        : TrackedObject(context), _root(std::make_shared<TrackingRoot>(context)) {
    }

    ObjectAnchor ObjHandle::toAnchor() const {
        if(*this) {
            return table().get(partial());
        } else {
            return {};
        }
    }
} // namespace data
