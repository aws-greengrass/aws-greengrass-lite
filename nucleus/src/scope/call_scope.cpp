#include "scope/call_scope.hpp"
#include "scope/context_full.hpp"

namespace scope {

    std::shared_ptr<CallScope> CallScope::create(
        const std::shared_ptr<Context> &context,
        const std::shared_ptr<data::TrackingRoot> &root,
        const std::shared_ptr<scope::NucleusCallScopeContext> &owningContext,
        const std::shared_ptr<scope::CallScope> &priorScope) {
        auto newScope{std::make_shared<CallScope>(context, owningContext, priorScope)};
        auto selfAnchor = root->anchor(newScope);
        newScope->setSelf(selfAnchor.getHandle());
        return newScope;
    }

    void CallScope::release() {
        data::ObjHandle selfHandle = getSelf();
        if(!selfHandle) {
            return;
        }
        auto selfAnchor = selfHandle.toAnchor();
        auto parent = selfAnchor.getRoot();
        parent->remove(selfAnchor);
    }
    void CallScope::beforeRemove(const data::ObjectAnchor &anchor) {
        TrackedObject::beforeRemove(anchor);
        _self = {};
        auto owningContext = _owningContext.lock();
        auto priorScope = _priorScope.lock(); // Can be null
        if(owningContext && owningContext->getCallScope().get() == this) {
            // handle associated with this scope deleted, need to drop to parent
            while(priorScope && !priorScope->_self) {
                // Must use a scope that has a valid handle
                priorScope = priorScope->_priorScope.lock();
            }
            // priorScope can be null here, with correct behavior
            owningContext->setCallScope(priorScope);
        }
    }

} // namespace scope
