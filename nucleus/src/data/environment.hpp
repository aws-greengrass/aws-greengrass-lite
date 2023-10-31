#pragma once
#include "config/config_manager.hpp"
#include "handle_table.hpp"
#include "string_table.hpp"
#include "tasks/expire_time.hpp"
#include <optional>
#include <shared_mutex>

namespace data {
    class SysProperties {
    private:
        mutable std::shared_mutex _mutex;
        std::map<std::string, std::string> _cache;

    public:
        static constexpr auto HOME = {"HOME"};

        SysProperties() = default;

        void parseEnv(char *envp[]); // NOLINT(*-avoid-c-arrays)

        std::optional<std::string> get(std::string_view name) const;

        void put(const std::string &name, const std::string &value);

        void put(std::string_view name, std::string_view value) {
            put(std::string(name), std::string(value));
        }

        bool exists(std::string_view name) const;

        void remove(std::string_view name);
    };

    struct Environment { // NOLINT(*-special-member-functions)
        HandleTable handleTable;
        StringTable stringTable;
        config::Manager configManager{*this};
        SysProperties sysProperties;
        std::shared_mutex sharedLocalTopicsMutex;
        std::mutex cycleCheckMutex;
        virtual ~Environment() = default;

        virtual tasks::ExpireTime translateExpires(int32_t delta);
    };
} // namespace data
