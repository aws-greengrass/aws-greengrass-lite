#pragma once
#include "config_timestamp.hpp"
#include "data/string_table.hpp"
#include "data/symbol_value_map.hpp"
#include "scope/context_full.hpp"

namespace config {

    /**
     * Base class for MergeUpdateTree or ReplaceUpdateTree
     */
    class UpdateBehaviorTree : private scope::UsesContext {
    public:
        UpdateBehaviorTree(const UpdateBehaviorTree &other) = delete;
        UpdateBehaviorTree(UpdateBehaviorTree &&) = delete;
        UpdateBehaviorTree &operator=(const UpdateBehaviorTree &other) = delete;
        UpdateBehaviorTree &operator=(UpdateBehaviorTree &&) = delete;
        virtual ~UpdateBehaviorTree() noexcept = default;

        virtual std::shared_ptr<UpdateBehaviorTree> getChildBehavior(data::Symbol &key) = 0;

        static constexpr const std::string_view WILDCARD = "*";

    protected:
        const Timestamp timestamp;
        scope::SharedContextMapper _symbolMapper;
        using TreeMap = data::SymbolValueMap<std::shared_ptr<UpdateBehaviorTree>>;
        TreeMap childOverride{_symbolMapper};
        std::shared_ptr<scope::Context> context = scope::context();

        UpdateBehaviorTree(const scope::UsingContext &context, const Timestamp &timestamp) :
              scope::UsesContext(context), timestamp(timestamp), _symbolMapper(context) {}

        virtual std::shared_ptr<UpdateBehaviorTree> getDefaultChildBehavior() = 0;
    };

    class MergeBehaviorTree : public UpdateBehaviorTree {
    public:
        MergeBehaviorTree(const scope::UsingContext &context, const Timestamp &timestamp) :
              UpdateBehaviorTree(context, timestamp) {}

        std::shared_ptr<UpdateBehaviorTree> getChildBehavior(data::Symbol &key) override {
            auto i = childOverride.find(key);
            if (i != childOverride.end()) {
                return i->second;
            }
            i = childOverride.find(context->intern(WILDCARD));
            if (i != childOverride.end()) {
                return i->second;
            }

            return getDefaultChildBehavior();
        }

    protected:
        std::shared_ptr<UpdateBehaviorTree> getDefaultChildBehavior() override {
            return std::make_shared<PrunedMergeBehaviorTree>(context, config::Timestamp::now());
        }

    private:
        class PrunedMergeBehaviorTree : public UpdateBehaviorTree,
                                        std::enable_shared_from_this<PrunedMergeBehaviorTree> {
        public:
            PrunedMergeBehaviorTree(const scope::UsingContext &context, const Timestamp &timestamp)
                : UpdateBehaviorTree(context, timestamp) {}

            std::shared_ptr<UpdateBehaviorTree> getChildBehavior(data::Symbol &key) override {

                return shared_from_this();
            }

            std::shared_ptr<UpdateBehaviorTree> getDefaultChildBehavior() override {
                return shared_from_this();
            }
        };
    };

    class ReplaceBehaviorTree : public UpdateBehaviorTree {
    public:
        ReplaceBehaviorTree(const scope::UsingContext &context, const Timestamp &timestamp) :
              UpdateBehaviorTree(context, timestamp) {}

        std::shared_ptr<UpdateBehaviorTree> getChildBehavior(data::Symbol &key) override {
            auto i = childOverride.find(key);
            if (i != childOverride.end()) {
                return i->second;
            }
            i = childOverride.find(context->intern(WILDCARD));
            if (i != childOverride.end()) {
                return i->second;
            }

            return getDefaultChildBehavior();
        }

    protected:
        std::shared_ptr<UpdateBehaviorTree> getDefaultChildBehavior() override {
            return std::make_shared<PrunedReplaceBehaviorTree>(context, config::Timestamp::now());
        }

    private:
        class PrunedReplaceBehaviorTree : public UpdateBehaviorTree,
                                          std::enable_shared_from_this<PrunedReplaceBehaviorTree> {
        public:
            PrunedReplaceBehaviorTree(const scope::UsingContext &context, const Timestamp &timestamp)
                : UpdateBehaviorTree(context, timestamp) {}

            std::shared_ptr<UpdateBehaviorTree> getChildBehavior(data::Symbol &key) override {
                return shared_from_this();
            }

            std::shared_ptr<UpdateBehaviorTree> getDefaultChildBehavior() override {
                return shared_from_this();
            }
        };
    };
} // namespace config
