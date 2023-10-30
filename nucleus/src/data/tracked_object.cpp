#include "tracked_object.hpp"
#include "environment.hpp"

namespace data {
    ObjectAnchor TrackingScope::anchor(const std::shared_ptr<TrackedObject> &obj) {
        if(!obj) {
            return {};
        }
        return _environment.handleTable.create(ObjectAnchor{obj, scopeRef()});
    }

    ObjectAnchor TrackingScope::reanchor(const data::ObjectAnchor &anchored) {
        return anchor(anchored.getBase());
    }

    ObjectAnchor TrackingScope::createRootHelper(const data::ObjectAnchor &anchor) {
        // assume handleTable may be locked on entry, beware of recursive locks
        std::unique_lock guard{_mutex};
        _roots.emplace(anchor.getHandle(), anchor.getBase());
        return anchor;
    }

    void TrackingScope::remove(const data::ObjectAnchor &anchor) {
        _environment.handleTable.remove(anchor);
    }

    void ObjectAnchor::release() {
        std::shared_ptr<TrackingScope> owner{_owner.lock()};
        if(owner) {
            // go via owner - owner has access to _environment
            owner->remove(*this);
        } else {
            // if owner has gone away, handle will be deleted
            // so nothing to do here
        }
    }

    void TrackingScope::removeRootHelper(const data::ObjectAnchor &anchor) {
        // always called from HandleTable
        // assume handleTable could be locked on entry, beware of recursive locks
        std::unique_lock guard{_mutex};
        _roots.erase(anchor.getHandle());
    }

    std::vector<ObjectAnchor> TrackingScope::getRootsHelper(
        const std::weak_ptr<TrackingScope> &assumedOwner
    ) {
        std::shared_lock guard{_mutex};
        std::vector<ObjectAnchor> copy;
        for(const auto &i : _roots) {
            ObjectAnchor anc{i.second, assumedOwner};
            copy.push_back(anc.withHandle(i.first));
        }
        return copy;
    }

    TrackingScope::~TrackingScope() {
        for(const auto &i : getRootsHelper({})) {
            remove(i);
        }
    }
} // namespace data
