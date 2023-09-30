#pragma once
#include "environment.h"
#include "../tasks/task.h"
#include "../pubsub/local_topics.h"
#include "../plugins/plugin_loader.h"
#include "../pubsub/local_topics.h"
#include "../tasks/task.h"

namespace data {
    struct Global {
        Environment environment;
        std::shared_ptr<tasks::TaskManager> taskManager {std::make_shared<tasks::TaskManager>(environment)};
        std::shared_ptr<pubsub::LocalTopics> lpcTopics {std::make_shared<pubsub::LocalTopics>(environment)};
        std::shared_ptr<plugins::PluginLoader> loader {std::make_shared<plugins::PluginLoader>(environment)};

        static Global & self() {
            static Global global;
            return global;
        }

        static Environment & env() {
            return self().environment;
        }
    };
}

