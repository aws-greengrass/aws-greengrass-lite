#include "sys_properties.hpp"

namespace lifecycle {
    void SysProperties::parseEnv(util::Span<char *> envs) {
        for(std::string_view env : envs) {
            if(auto pos = env.find('='); pos != std::string_view::npos) {
                put(env.substr(0, pos), env.substr(pos + 1));
            } else {
                put(env, {});
            }
        }
    }

    std::optional<std::string> SysProperties::get(std::string_view name) const {
        std::shared_lock guard{_mutex};
        if(auto i = _cache.find(name); i == _cache.end()) {
            return {};
        } else {
            return i->second;
        }
    }

    bool SysProperties::exists(std::string_view name) const {
        std::shared_lock guard{_mutex};
        return _cache.find(name) != _cache.cend();
    }

    void SysProperties::put(std::string name, std::string value) {
        std::unique_lock guard{_mutex};
        _cache.insert_or_assign(std::move(name), std::move(value));
    }

    void SysProperties::remove(std::string_view name) {
        std::unique_lock guard{_mutex};
        if(auto it = _cache.find(name); it != _cache.end()) {
            _cache.erase(it);
        }
    }

} // namespace lifecycle
