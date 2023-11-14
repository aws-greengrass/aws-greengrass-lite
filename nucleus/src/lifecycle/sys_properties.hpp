#pragma once
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>

namespace lifecycle {
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
} // namespace lifecycle
