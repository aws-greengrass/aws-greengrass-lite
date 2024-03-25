#pragma once
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span.hpp>
#include <string>
#include <string_view>

namespace lifecycle {
    using namespace std::string_view_literals;
    class SysProperties {
    private:
        mutable std::shared_mutex _mutex;
        std::map<std::string, std::string, std::less<>> _cache;

    public:
        static constexpr auto HOME = {"HOME"sv};

        SysProperties() = default;

        void parseEnv(util::Span<char *> envs);

        std::optional<std::string> get(std::string_view name) const;

        void put(std::string name, std::string value);

        inline void put(std::string_view name, std::string_view value) {
            put(std::string(name), std::string(value));
        }

        bool exists(std::string_view name) const;

        void remove(std::string_view name);
    };
} // namespace lifecycle
