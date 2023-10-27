#include "environment.hpp"
#include "tasks/expire_time.hpp"

tasks::ExpireTime data::Environment::translateExpires(int32_t delta) {
    // override this to enable time-based testing
    return tasks::ExpireTime::fromNowMillis(delta);
}

namespace data {
    void SysProperties::parseEnv(char *envp[]) { // NOLINT(*-avoid-c-arrays)
        char **p = envp;
        for(; *p != nullptr; ++p) { // NOLINT(*-pointer-arithmetic)
            char *key = *p;
            char *eq = key;
            for(; *eq != 0; ++eq) { // NOLINT(*-pointer-arithmetic)
                if(*eq == '=') {
                    break;
                }
            }
            if(*eq) {
                put(std::string_view(key, eq - key),
                    std::string_view(eq + 1)); // NOLINT(*-pointer-arithmetic)
            } else {
                put(std::string_view(key, eq - key), "");
            }
        }
    }

    std::optional<std::string> SysProperties::get(std::string_view name) const {
        std::shared_lock guard{_mutex};
        std::string key{name};
        const auto &i = _cache.find(key);
        if(i == _cache.end()) {
            return {};
        } else {
            return i->second;
        }
    }

    bool SysProperties::exists(std::string_view name) const {
        std::shared_lock guard{_mutex};
        std::string key{name};
        const auto &i = _cache.find(key);
        return i != _cache.end();
    }

    void SysProperties::put(const std::string &name, const std::string &value) {
        std::unique_lock guard{_mutex};
        _cache.insert_or_assign(name, value);
    }

    void SysProperties::remove(std::string_view name) {
        std::unique_lock guard{_mutex};
        std::string key{name};
        _cache.erase(key);
    }
} // namespace data
