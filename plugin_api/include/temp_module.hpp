#pragma once
#include <atomic>
#include <cpp_api.hpp>
#include <utility>

namespace util {

    /**
     * Manage scope of a temporary module
     */
    class TempModule {
        ggapi::ModuleScope _module;
        ggapi::ModuleScope _prev;

    public:
        TempModule() = delete;
        TempModule(const TempModule &) = delete;
        TempModule(TempModule &&) noexcept = default;
        TempModule &operator=(const TempModule &) = delete;
        TempModule &operator=(TempModule &&) noexcept = default;

        explicit TempModule(std::string_view name) : _module(create(name)) {

            _prev = _module.setActive();
        };

        explicit TempModule(ggapi::ModuleScope module) : _module(std::move(module)) {
            _prev = _module.setActive();
        };

        [[nodiscard]] static ggapi::ModuleScope create(std::string_view name) {
            return ggapi::ModuleScope::registerGlobalPlugin(name, ggapi::LifecycleCallback());
        }

        ggapi::ModuleScope &operator*() {
            return _module;
        }

        ggapi::ModuleScope *operator->() {
            return &_module;
        }

        bool release() noexcept {
            auto prev = _prev;
            _prev = {};
            try {
                std::ignore = prev.setActive();
                return true;
            } catch(...) {
                // TODO: need to log
                return false;
            }
        }

        ~TempModule() noexcept {
            release();
        }
    };

} // namespace util
