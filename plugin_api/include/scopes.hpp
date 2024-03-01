#pragma once

#include "api_callbacks.hpp"
#include "api_forwards.hpp"
#include "buffer_stream.hpp"
#include "c_api.hpp"
#include "handles.hpp"

namespace ggapi {

    /**
     * Module scope. For module-global data. Typically used for listeners.
     */
    class ModuleScope : public ObjHandle {
        void check() {
            if(getHandleId() != 0 && !isScope()) {
                throw std::runtime_error("Scope handle expected");
            }
        }

    public:
        constexpr ModuleScope() noexcept = default;
        ModuleScope(const ModuleScope &) noexcept = default;
        ModuleScope(ModuleScope &&) noexcept = default;
        ModuleScope &operator=(const ModuleScope &) noexcept = default;
        ModuleScope &operator=(ModuleScope &&) noexcept = default;
        ~ModuleScope() = default;

        explicit ModuleScope(const ObjHandle &other) : ObjHandle{other} {
            check();
        }

        explicit ModuleScope(const SharedHandle &handle) : ObjHandle{handle} {
            check();
        }

        [[nodiscard]] ModuleScope registerPlugin(
            Symbol componentName, const LifecycleCallbackLambda &callback);

        [[nodiscard]] ModuleScope registerPlugin(
            Symbol componentName, const LifecycleCallback &callback);

        [[nodiscard]] static ModuleScope registerGlobalPlugin(
            Symbol componentName, const LifecycleCallbackLambda &callback);

        [[nodiscard]] static ModuleScope registerGlobalPlugin(
            Symbol componentName, const LifecycleCallback &callback);

        [[nodiscard]] ModuleScope setActive() const {
            return callHandleApiThrowError<ModuleScope>(ggapiChangeModule, getHandleId());
        }

        [[nodiscard]] static ModuleScope current() {
            return callHandleApiThrowError<ModuleScope>(ggapiGetCurrentModule);
        }
    };

    /**
     * Abstract lifecycle callback
     */
    class LifecycleCallback : public ObjHandle {

        /**
         * Templated dispatch allows for capturing multiple arguments to pass to a callback
         * routine. Note that this enables the same callback approach as std::invoke and std::thread
         * where a typical use is (callbackMethod, this) but acceptable also to do something like
         * (callbackMethod, this, extraArg1, extraArg2).
         * @tparam Callable Lambda, function pointer, method, etc.
         * @tparam Args Prefix arguments, particularly optional This
         */
        template<typename Callable, typename... Args>
        class LifecycleDispatch : public CallbackManager::CaptureDispatch<Callable, Args...> {

        public:
            explicit LifecycleDispatch(Callable callable, Args... args)
                : CallbackManager::CaptureDispatch<Callable, Args...>{
                      std::move(callable), std::move(args)...} {
                static_assert(
                    std::is_invocable_r_v<bool, Callable, Args..., ModuleScope, Symbol, Struct>);
            }
            [[nodiscard]] Symbol type() const override {
                return {"lifecycle"};
            }
            [[nodiscard]] CallbackManager::Delegate prepare(
                Symbol callbackType, ggapiDataLen size, void *data) const override {

                auto &cb = this->template checkedStruct<ggapiLifecycleCallbackData>(
                    callbackType, size, data);
                return this->template prepareWithArgsRet<bool, ModuleScope, Symbol, Struct>(
                    [&cb](bool f) { cb.retWasHandled = f ? 1 : 0; },
                    ObjHandle::of<ModuleScope>(cb.moduleHandle),
                    Symbol(cb.phaseSymbol),
                    ObjHandle::of<Struct>(cb.dataStruct));
            }
        };

    public:
        constexpr LifecycleCallback() noexcept = default;

        explicit LifecycleCallback(const ObjHandle &other) : ObjHandle{other} {
        }

        /**
         * Create reference to a lifecycle callback.
         */
        template<typename Callable, typename... Args>
        static LifecycleCallback of(const Callable &callable, const Args &...args) {
            auto dispatch =
                std::make_unique<LifecycleDispatch<std::decay_t<Callable>, std::decay_t<Args>...>>(
                    callable, args...);
            return CallbackManager::self().registerWithNucleus<LifecycleCallback>(
                std::move(dispatch));
        }
    };

    inline ModuleScope ModuleScope::registerPlugin(
        Symbol componentName, const LifecycleCallback &callback) {
        required();
        return callApiReturnHandle<ModuleScope>([*this, componentName, callback]() {
            return ::ggapiRegisterPlugin(
                getHandleId(), componentName.asInt(), callback.getHandleId());
        });
    }

    inline ModuleScope ModuleScope::registerPlugin(
        Symbol componentName, const LifecycleCallbackLambda &callback) {
        return registerPlugin(componentName, LifecycleCallback::of(callback));
    }

    inline ModuleScope ModuleScope::registerGlobalPlugin(
        Symbol componentName, const LifecycleCallback &callback) {
        return callApiReturnHandle<ModuleScope>([componentName, callback]() {
            return ::ggapiRegisterPlugin(0, componentName.asInt(), callback.getHandleId());
        });
    }

    inline ModuleScope ModuleScope::registerGlobalPlugin(
        Symbol componentName, const LifecycleCallbackLambda &callback) {
        return registerGlobalPlugin(componentName, LifecycleCallback::of(callback));
    }

} // namespace ggapi
