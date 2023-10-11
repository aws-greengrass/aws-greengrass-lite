#pragma once
#include "config/config_manager.h"
#include "handle_table.h"
#include "string_table.h"
#include "tasks/expire_time.h"
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

        void put(std::string_view name, const std::string &value);

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

        virtual ExpireTime translateExpires(int32_t delta);
    };
} // namespace data
