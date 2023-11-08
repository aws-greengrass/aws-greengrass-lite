#include "tracked_object.hpp"
#include "environment.hpp"
#include "tasks/task.hpp"

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

    ObjectAnchor CallScope::getCurrent(Environment &env) {
        ObjHandle handle = CallScope::getThreadSelf();
        if(!handle) {
            handle = tasks::Task::getThreadSelf();
            if(!handle) {
                throw std::runtime_error("CallScope: no parent");
            }
        }
        return env.handleTable.get(handle);
    }

    std::shared_ptr<CallScope> CallScope::create(Environment &env) {
        std::shared_ptr<TrackingScope> parent{getCurrent(env).getObject<TrackingScope>()};
        auto newScope{std::make_shared<CallScope>(env)};
        auto selfAnchor = parent->anchor(newScope);
        newScope->setSelf(selfAnchor.getHandle());
        newScope->setThreadSelf();
        return newScope;
    }

    // NOLINTNEXTLINE(*-no-recursion)
    bool CallScope::checkIfParent(ObjHandle target) const {
        if(!target) {
            return false;
        }
        ObjHandle selfHandle = getSelf();
        if(target == getSelf()) {
            return true;
        }
        auto selfAnchor = _environment.handleTable.get(selfHandle);
        auto owner = selfAnchor.getOwner();
        auto ownerCallScope = std::dynamic_pointer_cast<CallScope>(owner);
        if(!ownerCallScope) {
            return false;
        }
        return ownerCallScope->checkIfParent(target);
    }

    void CallScope::release() {
        ObjHandle selfHandle = getSelf();
        if(!selfHandle) {
            return;
        }
        auto selfAnchor = _environment.handleTable.get(selfHandle);
        auto parent = selfAnchor.getOwner();
        parent->remove(selfAnchor);
    }

    void CallScope::beforeRemove(const ObjectAnchor &anchor) {
        if(anchor.getHandle() != getSelf()) {
            return; // only for controlling handle
        }
        std::shared_ptr<CallScope> callScopeParent =
            std::dynamic_pointer_cast<CallScope>(anchor.getOwner());
        ObjHandle threadHandle = getThreadSelf();
        if(!checkIfParent(threadHandle)) {
            // thread scope mismatch, we'll only allow if thread is a parent
            throw std::runtime_error("CallScope: scope is used incorrectly and thread scope "
                                     "mismatches this call scope");
        }
        if(threadHandle == getSelf()) {
            if(callScopeParent) {
                // parent is a call scope
                callScopeParent->setThreadSelf();
            } else {
                // (assume) parent is a task
                getSetThreadSelf({}, true);
            }
        }
    }

    LocalCallScope::~LocalCallScope() {
        if(_scopeData) {
            _scopeData->release();
            _scopeData.reset();
        }
    }
} // namespace data
