#pragma once

#include "data/shared_struct.hpp"
#include "data/string_table.hpp"
#include "pubsub/promise.hpp"
#include "subscriptions.hpp"
#include "tasks/task_callbacks.hpp"
#include <future>
#include <memory>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace lifecycle::kernel {
    class ComponentLoaderListener : public tasks::Callback {
    private:
        mutable std::shared_mutex _mutex;
        // ComponentType -> handler subscription topic

        std::unordered_map<std::string, std::string> _knownLoaders;
        std::atomic_bool _newLoader{false};

    public:
        using tasks::Callback::Callback;

        std::shared_ptr<pubsub::Future> invokeTopicCallback(
            const data::Symbol &, const std::shared_ptr<data::ContainerModelBase> &data) override {
            auto casted = std::dynamic_pointer_cast<data::SharedStruct>(data);
            if(!casted) {
                throw BadCastError{};
            }

            {
                std::string componentType = casted->get("componentSupportType").getString();
                std::string componentTopic = casted->get("componentSupportTopic").getString();
                if(componentType.empty() || componentTopic.empty()) {
                    throw std::invalid_argument{"Empty component support type or topic name"};
                }
                std::unique_lock lock{_mutex};
                auto emplaceResult =
                    _knownLoaders.emplace(std::move(componentType), std::move(componentTopic));
                if(emplaceResult.second) {
                    _newLoader.store(true);
                }
            }
            auto promise = std::make_shared<pubsub::Promise>(context());
            promise->setValue(std::make_shared<data::SharedStruct>(context()));
            return promise->getFuture();
        }

        bool hasNewLoader() {
            return _newLoader.exchange(false);
        }

        std::unordered_map<std::string, std::string> getLoaders() const {
            std::shared_lock lock{_mutex};
            return _knownLoaders;
        }

        std::optional<std::string> getLoader(const std::string &symbol) const {
            std::shared_lock lock{_mutex};
            auto found = _knownLoaders.find(symbol);
            if(found == _knownLoaders.end()) {
                return {};
            }
            return found->second;
        }
    };
} // namespace lifecycle::kernel
