#pragma once
#include <iostream>

#include <aws/crt/Api.h>

static Aws::Crt::ApiHandle apiHandle{};

class IpcAdapter {
    std::string_view _rootPath;

public:
    explicit IpcAdapter(std::string_view rootPath) : _rootPath(rootPath) {
    }

    void publishToTopic(std::string_view topicName, std::string_view message);

    void publishToIoTCore(
        std::string_view topicName, std::string_view message, std::string_view qos);

    void subscribeToTopic(std::string_view topicName);

    void subscribeToIoTCore(std::string_view topicName, std::string_view qos);
};
