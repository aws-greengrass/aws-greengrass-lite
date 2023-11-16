#pragma once

#include "config/config_timestamp.hpp"
#include "config/publish_queue.hpp"
#include "data/handle_table.hpp"
#include "data/safe_handle.hpp"
#include "data/shared_struct.hpp"
#include "data/string_table.hpp"
#include "tasks/expire_time.hpp"
#include "watcher.hpp"
#include <atomic>
#include <filesystem>
#include <optional>
#include <utility>
#include <iostream>

namespace scope {
    class Context;
}

namespace config {
    class Timestamp;
    class TopicElement;
    class ConfigNode;
    class Topic;
    class Topics;

    class Manager {
    private:
        std::weak_ptr<scope::Context> _context;
        std::shared_ptr<Topics> _root;
        PublishQueue _publishQueue;

        scope::Context &context() const {
            return *_context.lock();
        }

    public:
        explicit Manager(const std::shared_ptr<scope::Context> &context);

        std::shared_ptr<Topics> root() {
            return _root;
        }

        PublishQueue &publishQueue() {
            return _publishQueue;
        }

        Topic lookup(std::initializer_list<std::string> path);

        Topic lookup(Timestamp timestamp, std::initializer_list<std::string> path);

        std::shared_ptr<Topics> lookupTopics(std::initializer_list<std::string> path);

        std::shared_ptr<Topics> lookupTopics(
            Timestamp timestamp, std::initializer_list<std::string> path);

        std::optional<Topic> find(std::initializer_list<std::string> path);

        data::ValueType findOrDefault(
            const data::ValueType &defaultV, std::initializer_list<std::string> path);

        std::shared_ptr<config::Topics> findTopics(std::initializer_list<std::string> path);

        Manager &read(const std::filesystem::path &path);
    };
} // namespace config
