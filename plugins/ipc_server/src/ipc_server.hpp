#pragma once


#include <forward_list>
#include <limits>
#include <optional>
#include <plugin.hpp>
#include <random>

#include "cpp_api.hpp"
#include "server_listener.hpp"
#include <aws/crt/Api.h>

class IpcServer final : public ggapi::Plugin {
private:
    using MutexType = std::shared_mutex;
    template<template<class> class Lock>
    static constexpr bool is_lockable = std::is_constructible_v<Lock<MutexType>, MutexType &>;
    // TODO: This needs to come from host-environment plugin
    static constexpr std::string_view SOCKET_NAME = "gglite-ipc.socket";

    ggapi::Struct cliHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct);
    static std::string generateIpcToken();

    std::atomic<ggapi::Struct> _system;
    std::atomic<ggapi::Struct> _config;
    std::atomic<ggapi::Struct> _configRoot;

public:
    bool onBootstrap(ggapi::Struct data) override;
    bool onBind(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
    bool onTerminate(ggapi::Struct data) override;
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;

    static IpcServer &get() {
        static IpcServer instance{};
        return instance;
    }

    template<template<class> class Lock = std::unique_lock>
    std::enable_if_t<is_lockable<Lock>, Lock<MutexType>> lock() & {
        return Lock{mutex};
    }

private:
    MutexType mutex;
    std::shared_ptr<Listener> _listener;
};
