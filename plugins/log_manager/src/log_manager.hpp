#pragma once

#include <plugin.hpp>

class LogManager : public ggapi::Plugin {
private:
    mutable std::shared_mutex _mutex;
    ggapi::Struct _system;
    ggapi::Struct _config;

public:
    LogManager() = default;
    bool onInitialize(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
    bool onStop(ggapi::Struct data) override;
    bool onError_stop(ggapi::Struct data) override;

    static LogManager &get() {
        static LogManager instance{};
        return instance;
    }
};
