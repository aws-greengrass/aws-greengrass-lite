#pragma once
#include "cloud_downloader.hpp"

constexpr std::string_view START = "start";
constexpr std::string_view RUN = "run";
constexpr std::string_view TERMINATE = "terminate";

class TestCloudDownloader : public CloudDownloader {
    ggapi::ModuleScope _moduleScope;

public:
    explicit TestCloudDownloader(ggapi::ModuleScope moduleScope)
        : CloudDownloader(), _moduleScope(moduleScope) {
    }

    bool executePhase(std::string_view phase) {
        // TODO: Return before afterLifecycle?
        beforeLifecycle(ggapi::Symbol{phase}, ggapi::Struct::create());
        bool status = lifecycle(_moduleScope, ggapi::Symbol{phase}, ggapi::Struct::create());
        afterLifecycle(ggapi::Symbol{phase}, ggapi::Struct::create());
        return status;
    };

    bool startLifecycle() {
        // TODO: gotta be a better way to do this
        return executePhase(START) && executePhase(RUN);
    }

    bool stopLifecycle() {
        return executePhase(TERMINATE);
    }
};

class cloudPubCallback{
public:
    virtual ~cloudPubCallback() = default;
    virtual void sendToTopic() = 0;
};
