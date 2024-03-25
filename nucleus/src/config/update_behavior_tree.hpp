#pragma once
#include "../data/string_table.hpp"
#include "config_timestamp.hpp"
#include "data/string_table.hpp"
#include "data/symbol_value_map.hpp"
#include "scope/context_full.hpp"
#include "scope/mapper.hpp"

namespace config {

    /**
     * Base class for MergeUpdateTree or ReplaceUpdateTree
     */
    class UpdateBehaviorTree : private scope::UsesContext {
    private:
        Timestamp timestamp;
        scope::SharedContextMapper _symbolMapper;
        using TreeMap = data::SymbolValueMap<std::shared_ptr<UpdateBehaviorTree>>;
        TreeMap childOverride{_symbolMapper};
        std::shared_ptr<scope::Context> context = scope::context();

        virtual std::shared_ptr<UpdateBehaviorTree> getDefaultChildBehavior() = 0;

    protected:
        UpdateBehaviorTree(Timestamp timestamp, TreeMap childOverride),
            timestamp(timestamp) : childOverride(childOverride) {}

    public:
        UpdateBehaviorTree(const scope::UsingContext &context)
            : scope::UsesContext(context), _symbolMapper(context) {
        }
        virtual ~UpdateBehaviorTree() noexcept = default;

        static constexpr const char* const WILDCARD = "*";

        virtual std::shared_ptr<UpdateBehaviorTree> getChildBehavior(data::Symbol &key) {
            auto i = childOverride.find(key);
            if (i != childOverride.end()) {
                return i->second;
            }
            // how to get a symbol value from a string? Want to use wildcard key here
            i = childOverride.find(context->s);
            if (i != childOverride.end()) {
                return i->second;
            }

            return getDefaultChildBehavior();
        }

    };

    class MergeUpdateTree : public UpdateBehaviorTree {
    private:
        Timestamp timestamp;
        static constexpr const char* const UPDATE_BEHAVIOR = "MERGE";

    public:
        MergeUpdateTree(Timestamp &timestamp) : timestamp(timestamp) {

        }

        std::shared_ptr<UpdateBehaviorTree> getDefaultChildBehavior() override {
            //TODO: return a MergeUpdateTree that contains an empty childOverride map
        }


    };

    class ReplaceBehaviorTree : public UpdateBehaviorTree {
    private:
        Timestamp timestamp;
        static constexpr const char* const UPDATE_BEHAVIOR = "REPLACE";

        //TODO: Reflect MergeUpdateTree implementation, but with replace used instead
    };
} // namespace config