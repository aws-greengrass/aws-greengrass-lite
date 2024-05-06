#pragma once

#include <cpp_api.hpp>
#include <logging.hpp>
#include <memory>
#include <plugin.hpp>
#include <shared_device_sdk.hpp>
#include <shared_mutex>

class LifecycleIPC : public ggapi::Plugin {
    struct Keys {
        ggapi::Symbol payload{"payload"};
        ggapi::Symbol shape{"shape"};
        ggapi::Symbol terminate{"terminate"};
    };

    mutable std::shared_mutex _mutex;
    ggapi::Struct _nucleus;
    ggapi::Struct _system;

    ggapi::Subscription _updateStateSubs;
    ggapi::Subscription _subscribeToComponentUpdatesSubs;
    ggapi::Subscription _deferComponentUpdateSubs;
private:
    static const Keys keys;

    ggapi::Promise updateStateHandler(ggapi::Symbol, const ggapi::Container &args);
    void updateState(const ggapi::Struct &args, ggapi::Promise promise);
    ggapi::Promise subscribeToComponentUpdatesHandler(ggapi::Symbol, const ggapi::Container &args);
    void subscribeToComponentUpdates(const ggapi::Struct &args, ggapi::Promise promise);
    ggapi::Promise deferComponentUpdatesHandler(ggapi::Symbol, const ggapi::Container &args);
    void deferComponentUpdates(const ggapi::Struct &args, ggapi::Promise promise);
public:
    void onInitialize(ggapi::Struct data) override;
    void onStart(ggapi::Struct data) override;
    void onStop(ggapi::Struct data) override;

    static LifecycleIPC &get() {
        static LifecycleIPC instance{};
        return instance;
    }
};
