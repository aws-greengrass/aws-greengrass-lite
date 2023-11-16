#pragma once
#include "data/tracked_object.hpp"
#include "scope/context.hpp"
#include <memory>

namespace scope {

    /**
     * A scope that is intended to be stack based, is split into two, and should be used
     * via the CallScope stack class.
     */
    class CallScope : public data::TrackingScope {
        data::ObjHandle _self;
        std::weak_ptr<scope::NucleusCallScopeContext> _owningContext;
        std::weak_ptr<scope::CallScope> _priorScope;
        mutable std::shared_mutex _mutex;

        void setSelf(const data::ObjHandle &handle) {
            _self = handle;
        }

    public:
        explicit CallScope(
            const std::shared_ptr<Context> &context,
            const std::shared_ptr<scope::NucleusCallScopeContext> &owningContext,
            const std::shared_ptr<scope::CallScope> &priorScope)
            : data::TrackingScope(context), _owningContext(owningContext), _priorScope(priorScope) {
        }

        [[nodiscard]] static std::shared_ptr<CallScope> create(
            const std::shared_ptr<Context> &context,
            const std::shared_ptr<data::TrackingRoot> &root,
            const std::shared_ptr<scope::NucleusCallScopeContext> &owningThread,
            const std::shared_ptr<scope::CallScope> &priorScope);
        void release();

        void beforeRemove(const data::ObjectAnchor &anchor) override;

        data::ObjHandle getSelf() {
            return _self;
        }
    };

} // namespace scope
