#pragma once
#include "cloud_downloader.hpp"

constexpr std::string_view DISCOVER = "discover";
constexpr std::string_view START = "start";
constexpr std::string_view RUN = "run";
constexpr std::string_view TERMINATE = "stop";

class TestCloudDownloader : public CloudDownloader {
    ggapi::ModuleScope _moduleScope;

public:
    explicit TestCloudDownloader(ggapi::ModuleScope moduleScope)
        : CloudDownloader(), _moduleScope(moduleScope) {
        auto init = ggapi::Struct::create().put(ggapi::Plugin::MODULE, _moduleScope);
        internalBind(init);
    }

    void executePhase(std::string_view phase) {
        lifecycle(ggapi::Symbol{phase}, ggapi::Struct::create());
    };

    void startLifecycle() {
        // TODO: gotta be a better way to do this
        executePhase(DISCOVER);
        executePhase(START);
        executePhase(RUN);
    }

    void stopLifecycle() {
        executePhase(TERMINATE);
    }
};

class cloudPubCallback {
public:
    virtual ~cloudPubCallback() = default;
    virtual void sendToTopic() = 0;
};
