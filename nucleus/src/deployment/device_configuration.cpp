#include "device_configuration.hpp"
#include "lifecycle/kernel.hpp"

namespace deployment {
    std::string DeviceConfiguration::getNucleusComponentName() {
        return {};
        //        std::unique_lock guard{_mutex};
        //        if(!_nucleusComponentNameCache.empty()) {
        //            if(_kernel.findServiceTopic(_nucleusComponentNameCache)) {
        //                return _nucleusComponentNameCache;
        //            }
        //        }
    }

    DeviceConfiguration::DeviceConfiguration(
        data::Environment &environment, lifecycle::Kernel &kernel
    )
        : _environment(environment), _kernel(kernel), names(environment) {
        // TODO: deTildeValidator
        // TODO: regionValidator
        // TODO: loggingConfig
        // TODO: componentMaxSizeBytes
        // TODO: pollingFreqSeconds
        // TODO: S3-Endpoint-name
        // TODO: OnAnyChange
    }
} // namespace deployment
