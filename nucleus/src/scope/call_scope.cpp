#include "scope/call_scope.hpp"
#include "scope/context_full.hpp"

namespace scope {

    std::shared_ptr<CallScope> CallScope::create(
        const std::shared_ptr<Context> &context, const std::shared_ptr<data::TrackingRoot> &root) {
        auto newScope{std::make_shared<CallScope>(context)};
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

} // namespace scope