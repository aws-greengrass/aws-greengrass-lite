#include "lifecycle_ipc.hpp"
#include <algorithm>
#include <cpp_api.hpp>
#include <ipc_standard_errors.hpp>
#include <mutex>
#include <ipc_interfaces/lifecycle_ipc.hpp>

const auto LOG = ggapi::Logger::of("LifecycleIPC");

/* UpdateState IPC Command */
ggapi::Promise LifecycleIPC::updateStateHandler(
        ggapi::Symbol symbol, const ggapi::Container &argsBase) {
    ggapi::Struct args{argsBase};

    // TODO: Implement UpdateState IPC handler

    return ggapi::Promise::create().async(
            &LifecycleIPC::updateState, this, ggapi::Struct(args));
}

void LifecycleIPC::updateState(const ggapi::Struct &args, ggapi::Promise promise) {
    // TODO: Implement UpdateState
    promise.fulfill([&]() {
        return ggapi::Struct::create();
    });
}

/* SubscribeToComponentUpdates IPC Command */
ggapi::Promise LifecycleIPC::subscribeToComponentUpdatesHandler(
        ggapi::Symbol symbol, const ggapi::Container &argsBase) {
    ggapi::Struct args{argsBase};

    // TODO: Implement SubscribeToComponentUpdates IPC handler

    return ggapi::Promise::create().async(
            &LifecycleIPC::subscribeToComponentUpdates, this, ggapi::Struct(args));
}

void LifecycleIPC::subscribeToComponentUpdates(const ggapi::Struct &args, ggapi::Promise promise) {
    // TODO: Implement SubscribeToComponentUpdates
    promise.fulfill([&]() {
        return ggapi::Struct::create();
    });
}

/* DeferComponentUpdate IPC Command */
ggapi::Promise LifecycleIPC::deferComponentUpdatesHandler(
        ggapi::Symbol symbol, const ggapi::Container &argsBase) {
    ggapi::Struct args{argsBase};

    // TODO: Implement DeferComponentUpdate IPC handler

    return ggapi::Promise::create().async(
            &LifecycleIPC::deferComponentUpdates, this, ggapi::Struct(args));
}

void LifecycleIPC::deferComponentUpdates(const ggapi::Struct &args, ggapi::Promise promise) {
    // TODO: Implement DeferComponentUpdate
    promise.fulfill([&]() {
        return ggapi::Struct::create();
    });
}

/* Plugin Lifecycle Implementation */
void LifecycleIPC::onInitialize(ggapi::Struct data) {
    LOG.atInfo().log("Initializing Lifecycle IPC Plugin");
    std::ignore = util::getDeviceSdkApiHandle();
    data.put("name", "aws.greengrass.lifecycle_ipc");

    std::unique_lock guard{_mutex};
    _nucleus = data.getValue<ggapi::Struct>({"nucleus"});
    _system = data.getValue<ggapi::Struct>({"system"});
}

void LifecycleIPC::onStart(ggapi::Struct data) {
    LOG.atInfo().log("Starting Lifecycle IPC Plugin");
    std::shared_lock guard{_mutex};
    _updateStateSubs = ggapi::Subscription::subscribeToTopic(
            ipc_interfaces::lifecycle_ipc::updateStateTopic,
            ggapi::TopicCallback::of(&LifecycleIPC::updateStateHandler, this));
    _subscribeToComponentUpdatesSubs = ggapi::Subscription::subscribeToTopic(
            ipc_interfaces::lifecycle_ipc::subscribeToComponentUpdatesTopic,
            ggapi::TopicCallback::of(&LifecycleIPC::subscribeToComponentUpdatesHandler, this));
    _deferComponentUpdateSubs = ggapi::Subscription::subscribeToTopic(
            ipc_interfaces::lifecycle_ipc::deferComponentUpdateTopic,
            ggapi::TopicCallback::of(&LifecycleIPC::deferComponentUpdatesHandler, this));
}

void LifecycleIPC::onStop(ggapi::Struct structData) {
    LOG.atInfo().log("Stopping Lifecycle IPC Plugin");

    if(_updateStateSubs.isSubscription()) {
        _updateStateSubs.close();
    }
    if(_subscribeToComponentUpdatesSubs.isSubscription()) {
        _subscribeToComponentUpdatesSubs.close();
    }
    if(_deferComponentUpdateSubs.isSubscription()) {
        _deferComponentUpdateSubs.close();
    }
}
