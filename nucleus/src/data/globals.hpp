#pragma once
#include "environment.hpp"
#include "plugins/plugin_loader.hpp"
#include "pubsub/local_topics.hpp"
#include "tasks/task_manager.hpp"

namespace data {
    struct Global {
        Environment environment;
        tasks::TaskManagerContainer taskManager{environment};
        std::shared_ptr<pubsub::PubSubManager> lpcTopics{
            std::make_shared<pubsub::PubSubManager>(environment)};
        std::shared_ptr<plugins::PluginLoader> loader{
            std::make_shared<plugins::PluginLoader>(environment)};

        static Global &self() {
            static Global global;
            return global;
        }

        static Environment &env() {
            return self().environment;
        }
    };
} // namespace data
