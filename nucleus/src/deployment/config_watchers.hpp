#pragma once
#include "config/config_manager.hpp"
#include "deployment/device_configuration.hpp"
#include "lifecycle/kernel.hpp"

//
// TODO: Follow the templated capture model used on Plugin side
//

namespace deployment {
    //
    class ApplyDeTilde : public config::Watcher {
        lifecycle::Kernel &_kernel;

    public:
        explicit ApplyDeTilde(lifecycle::Kernel &kernel) : _kernel(kernel) {
        }

        //
        std::optional<data::ValueType> validate(
            const std::shared_ptr<config::Topics> &topics,
            data::Symbol key,
            const data::ValueType &proposed,
            const data::ValueType &currentValue) override {

            data::StructElement val(proposed);
            auto path = _kernel.getPaths()->deTilde(val.getString());
            return path.generic_string();
        }
    };

    class RegionValidator : public config::Watcher {
    public:
        std::optional<data::ValueType> validate(
            const std::shared_ptr<config::Topics> &topics,
            data::Symbol key,
            const data::ValueType &proposed,
            const data::ValueType &currentValue) override {
            // TODO: Fill in behavior
            return {};
        }
    };

    class InvalidateCache : public config::Watcher {
        std::weak_ptr<DeviceConfiguration> _config;

    public:
        explicit InvalidateCache(const std::shared_ptr<DeviceConfiguration> &config)
            : _config(config) {
        }

        void childChanged(
            const std::shared_ptr<config::Topics> &topics,
            data::Symbol key,
            config::WhatHappened changeType) override {
            //
            std::shared_ptr<DeviceConfiguration> config{_config};
            if(config) {
                config->invalidateCachedResult();
            }
        }
    };

    class LoggingConfigWatcher : public config::Watcher {
        std::weak_ptr<DeviceConfiguration> _config;

    public:
        explicit LoggingConfigWatcher(const std::shared_ptr<DeviceConfiguration> &config)
            : _config(config) {
        }

        void childChanged(
            const std::shared_ptr<config::Topics> &topics,
            data::Symbol key,
            config::WhatHappened changeType) override {

            std::shared_ptr<DeviceConfiguration> config{_config};
            if(config) {
                config->handleLoggingConfigurationChanges(topics, key, changeType);
            }
        }
        void initialized(
            const std::shared_ptr<config::Topics> &topics,
            data::Symbol key,
            config::WhatHappened changeType) override {

            std::shared_ptr<DeviceConfiguration> config{_config};
            if(config) {
                config->handleLoggingConfigurationChanges(topics, key, changeType);
            }
        }
    };

} // namespace deployment
