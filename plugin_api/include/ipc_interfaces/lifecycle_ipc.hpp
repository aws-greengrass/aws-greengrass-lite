#pragma once
#include <api_archive.hpp>
#include <api_standard_errors.hpp>

namespace ipc_interfaces::lifecycle_ipc {
    /**
     * UpdateState IPC call takes in a LifecycleState of either RUNNING or ERRORED and updates
     * the state of the component to the provided state. It does not provide information in the
     * response.
     *
     * TODO: Descriptions of other commands
     */
    inline static const ggapi::Symbol updateStateTopic{"IPC::aws.greengrass#UpdateState"};
    inline static const ggapi::Symbol subscribeToComponentUpdatesTopic{"IPC::aws.greengrass#SubscribeToComponentUpdates"};
    inline static const ggapi::Symbol deferComponentUpdateTopic{"IPC::aws.greengrass#DeferComponentUpdate"};

    // TODO: Add command ins/outs to the interface
}