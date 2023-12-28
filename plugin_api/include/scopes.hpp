#pragma once

#include "api_callbacks.hpp"
#include "api_forwards.hpp"
#include "buffer_stream.hpp"
#include "c_api.hpp"
#include "handles.hpp"

namespace ggapi {

    /**
     * Scopes are a class of handles that are used as targets for anchoring other handles.
     * See the subclasses to understand the specific types of scopes. There are currently two
     * kinds of scopes - Module scope (for the duration plugin is loaded), and Call scope
     * (stack-based).
     */
    class Scope : public ObjHandle {

        void check() {
            if(getHandleId() != 0 && !isScope()) {
                throw std::runtime_error("Scope handle expected");
            }
        }

    public:
        constexpr Scope() noexcept = default;
        Scope(const Scope &) noexcept = default;
        Scope(Scope &&) noexcept = default;
        Scope &operator=(const Scope &) noexcept = default;
        Scope &operator=(Scope &&) noexcept = default;
        ~Scope() = default;

        explicit Scope(const ObjHandle &other) : ObjHandle(other) {
            check();
        }

        explicit Scope(uint32_t handle) : ObjHandle(handle) {
            check();
        }

        //
        // Creates a subscription. A subscription is tied to scope and will be unsubscribed if
        // the scope is deleted.
        //
        [[nodiscard]] Subscription subscribeToTopic(
            Symbol topic, const TopicCallbackLambda &callback);

        //
        // Generic form of subscribeToTopic
        //
        [[nodiscard]] Subscription subscribeToTopic(Symbol topic, TopicCallback callback);

        //
        // Anchor an object against this scope.
        //
        template<typename T>
        [[nodiscard]] T anchor(T otherHandle) const;
    };

    /**
     * Module scope. For module-global data. Typically used for listeners.
     */
    class ModuleScope : public Scope {
    public:
        constexpr ModuleScope() noexcept = default;
        ModuleScope(const ModuleScope &) noexcept = default;
        ModuleScope(ModuleScope &&) noexcept = default;
        ModuleScope &operator=(const ModuleScope &) noexcept = default;
        ModuleScope &operator=(ModuleScope &&) noexcept = default;
        ~ModuleScope() = default;

        explicit ModuleScope(const ObjHandle &other) : Scope{other} {
        }

        explicit ModuleScope(uint32_t handle) : Scope{handle} {
        }

        [[nodiscard]] ModuleScope registerPlugin(
            Symbol componentName, const LifecycleCallbackLambda &callback);

        [[nodiscard]] ModuleScope registerPlugin(Symbol componentName, LifecycleCallback callback);

        [[nodiscard]] static ModuleScope registerGlobalPlugin(
            Symbol componentName, const LifecycleCallbackLambda &callback);

        [[nodiscard]] static ModuleScope registerGlobalPlugin(
            Symbol componentName, LifecycleCallback callback);

        ModuleScope setActive() {
            return callApiReturnHandle<ModuleScope>(
                [this]() { return ::ggapiChangeModule(getHandleId()); });
        }

        [[nodiscard]] static ModuleScope current() {
            return callApiReturnHandle<ModuleScope>([]() { return ::ggapiGetCurrentModule(); });
        }
    };

    /**
     * Temporary (stack-local) scope, that is default scope for objects.
     */
    class CallScope : public Scope {

    public:
        /**
         * Use only in stack context, push and create a stack-local call scope
         * that is popped when object is destroyed.
         */
        explicit CallScope() : Scope() {
            _handle = callApiReturn<uint32_t>([]() { return ::ggapiCreateCallScope(); });
        }

        CallScope(const CallScope &) = delete;
        CallScope &operator=(const CallScope &) = delete;
        CallScope(CallScope &&) noexcept = delete;
        CallScope &operator=(CallScope &&) noexcept = delete;

        void release() noexcept {
            if(_handle) {
                ::ggapiReleaseHandle(_handle); // do not (re)throw exception
                _handle = 0;
            }
        }

        ~CallScope() noexcept {
            release();
        }

        static Scope newCallScope() {
            return callApiReturnHandle<Scope>([]() { return ::ggapiCreateCallScope(); });
        }

        static Scope current() {
            return callApiReturnHandle<Scope>([]() { return ::ggapiGetCurrentCallScope(); });
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
        class LifecycleDispatch : public CallbackManager::CallbackDispatch {

            const Callable _callable;
            const std::tuple<Args...> _args;

        public:
            explicit LifecycleDispatch(Callable callable, Args &&...args)
                : _callable{std::move(callable)}, _args{std::forward<Args>(args)...} {
                static_assert(
                    std::is_invocable_r_v<bool, Callable, Args..., ModuleScope, Symbol, Struct>);
            }
            [[nodiscard]] Symbol type() const override {
                return {"lifecycle"};
            }
            [[nodiscard]] CallbackManager::Delegate prepare(
                uint32_t callbackType, uint32_t size, const void *data) const override {
                assertCallbackType(Symbol(callbackType));
                auto &cb = checkedStruct<LifecycleCallbackData>(size, data);
                auto target = _callable;
                auto module = ModuleScope(cb.moduleHandle);
                auto phase = Symbol(cb.phaseSymbol);
                auto dataStruct = Struct(cb.dataStruct);
                auto args = std::tuple_cat(_args, std::tuple{module, phase, dataStruct});
                return [target, args]() {
                    bool f = std::apply(target, args);
                    return static_cast<uint32_t>(f);
                };
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
        static LifecycleCallback of(const Callable &callable, Args &&...args) {
            auto dispatch = std::make_unique<LifecycleDispatch<Callable, Args...>>(
                callable, std::forward<Args>(args)...);
            return CallbackManager::self().registerWithNucleus<LifecycleCallback>(
                std::move(dispatch));
        }
    };

    template<typename T>
    inline T Scope::anchor(T otherHandle) const {
        required();
        static_assert(std::is_base_of_v<ObjHandle, T>);
        return callApiReturnHandle<T>([this, otherHandle]() {
            return ::ggapiAnchorHandle(getHandleId(), otherHandle.getHandleId());
        });
    }

} // namespace ggapi
