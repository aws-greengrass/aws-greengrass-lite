#pragma once
#include <optional>

namespace config {
    class Topics;

    //
    // Inside the Nucleus, there are some keys that have side effects, handled through watchers
    // Assume these are internal to Nucleus, but can be extended by creating a special watcher
    // container to map to pub-sub
    //
    enum class WhatHappened : int32_t {
        never = 0,
        changed = 1 << 0,
        initialized = 1 << 1,
        childChanged = 1 << 2,
        removed = 1 << 3,
        childRemoved = 1 << 4,
        timestampUpdated = 1 << 5,
        interiorAdded = 1 << 6,
        validation = 1 << 7,
        all = -1
    };

    // allow combining of bit-flags
    inline WhatHappened operator|(WhatHappened left, WhatHappened right) {
        return static_cast<WhatHappened>(
            static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
    }

    // allow masking of bit-flags
    inline WhatHappened operator&(WhatHappened left, WhatHappened right) {
        return static_cast<WhatHappened>(
            static_cast<uint32_t>(left) & static_cast<uint32_t>(right));
    }

    class Watcher {
    public:
        Watcher() = default;
        Watcher(const Watcher &) = delete;
        Watcher(Watcher &&) = delete;
        Watcher &operator=(const Watcher &) = delete;
        Watcher &operator=(Watcher &&) = delete;
        virtual ~Watcher() = default;

        [[nodiscard]] virtual std::optional<data::ValueType> validate(
            const std::shared_ptr<Topics> &topics,
            data::Symbol key,
            const data::ValueType &proposed,
            const data::ValueType &currentValue) {
            return {};
        }

        virtual void changed(
            const std::shared_ptr<Topics> &topics, data::Symbol key, WhatHappened changeType) {
        }

        virtual void childChanged(
            const std::shared_ptr<Topics> &topics, data::Symbol key, WhatHappened changeType) {
        }

        virtual void initialized(
            const std::shared_ptr<Topics> &topics, data::Symbol key, WhatHappened changeType) {
        }
    };

} // namespace config
