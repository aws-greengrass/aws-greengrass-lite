#pragma once

#include "api_callbacks.hpp"
#include "api_errors.hpp"
#include "api_forwards.hpp"
#include "c_api.hpp"
#include "containers.hpp"
#include "handles.hpp"
#include "scopes.hpp"
#include "util.hpp"

namespace ggapi {
    class Promise;

    /**
     * A "Future" is common base between Promise and AsyncFuture, and is analogous to Java Future
     * and c++ std::future.
     */
    class Future : public ObjHandle {
        void check() {
            if(getHandleId() != 0 && !isFuture()) {
                throw std::runtime_error("Future handle expected");
            }
        }

    public:
        constexpr Future() noexcept = default;

        explicit Future(const ObjHandle &other) : ObjHandle{other} {
            check();
        }

        explicit Future(const SharedHandle &handle) : ObjHandle{handle} {
            check();
        }

        [[nodiscard]] Container getValue() const {
            return callHandleApiThrowError<Container>(::ggapiFutureGetValue, getHandleId());
        }

        [[nodiscard]] bool isValid() const {
            return callBoolApiThrowError(::ggapiFutureIsValid, getHandleId());
        }

        void wait() const {
            std::ignore = wait(-1);
        }

        [[nodiscard]] bool wait(int32_t timeout) const {
            return callBoolApiThrowError(::ggapiFutureWait, getHandleId(), timeout);
        }

        [[nodiscard]] Container waitAndGetValue(int32_t timeout = -1) const {
            std::ignore = wait(timeout);
            return getValue();
        }

        /**
         * Create an action to be performed when the future completes. Action is executed in same
         * thread that completes the promise.
         */
        void whenValid(const FutureCallback &callback) const;

        /**
         * Create an action to be performed when the future completes. Action is executed in same
         * thread that completes the promise.
         */
        template<typename Callable, typename... Args>
        void whenValid(const Callable &callable, Args &&...args) const;

        /**
         * Convenience function for chaining continuations. Callback is called with
         * (nextPromise,prevFuture) where nextPromise is the new promise that the callback is to
         * complete, and prevFuture is the future that was completed.
         */
        template<typename Callable, typename... Args>
        Promise andThen(const Callable &callable, Args &&...args) const;
    };

    /**
     * A Promise is a container for a future value or error. Unlike the Future base class, a value
     * or error can be set. this is Analogous to Java CompletableFuture, and c++ std::promise.
     */
    class Promise : public Future {
        void check() const {
            if(getHandleId() != 0 && !isPromise()) {
                throw std::runtime_error("Promise handle expected");
            }
        }

    public:
        constexpr Promise() noexcept = default;

        explicit Promise(const ObjHandle &other) : Future{other} {
            check();
        }

        explicit Promise(const SharedHandle &handle) : Future{handle} {
            check();
        }

        /**
         * @return new unfulfilled promise object
         */
        [[nodiscard]] static Promise create() {
            return callHandleApiThrowError<Promise>(::ggapiCreatePromise);
        }

        /**
         * Create a fulfilled promise - which handles the most typical case
         * when no async fulfilment is needed.
         *
         * @param value Value to assign
         * @return new fulfilled promise
         */
        [[nodiscard]] static Promise of(const Container &value) {
            auto p = create();
            p.setValue(value);
            return p;
        }

        /**
         * Retrieve an un-modifiable future from the promise. The future cannot be re-cast to
         * a promise (can be monitored, cannot be fulfilled)
         *
         * @return future
         */
        [[nodiscard]] Future toFuture() const {
            return callHandleApiThrowError<Future>(::ggapiFutureFromPromise, getHandleId());
        }

        /**
         * Immediately execute callback in a worker. The async callback fulfills a promise.
         */
        template<typename Callable, typename... Args>
        [[nodiscard]] Promise &async(const Callable &callable, Args &&...args);

        /**
         * Execute callback in a worker after a delay. The async callback fulfills a promise.
         */
        template<typename Callable, typename... Args>
        [[nodiscard]] Promise &later(uint32_t delay, const Callable &callable, Args &&...args);

        void setValue(const Container &value) {
            callApiThrowError(::ggapiPromiseSetValue, getHandleId(), value.getHandleId());
        }

        void setError(const GgApiError &error) {
            uint32_t kindId = error.kind().asInt();
            std::string message{error.what()};
            callApiThrowError(
                ::ggapiPromiseSetError, getHandleId(), kindId, message.data(), message.length());
        }

        void cancel() const {
            callApiThrowError(::ggapiPromiseCancel, getHandleId());
        }

        template<typename Func, typename... Args>
        inline Container fulfill(Func &&f, Args &&...args);
    };

    /**
     * A Future set aids in a call-All pattern to collect all futures.
     */
    class FutureSet {
        std::vector<Future> _futures;
        mutable std::shared_mutex _mutex;

    public:
        explicit FutureSet(std::vector<Future> futures) : _futures(std::move(futures)) {
        }

        /**
         * @return Number of futures
         */
        [[nodiscard]] uint32_t size() const {
            std::shared_lock guard{_mutex};
            return _futures.size();
        }

        /**
         * @return Number of valid futures
         */
        [[nodiscard]] uint32_t ready() const {
            std::shared_lock guard{_mutex};
            return std::count_if(
                _futures.begin(), _futures.end(), [](const Future &f) { return f.isValid(); });
        }

        /**
         * @return Number of pending (not yet valid) futures
         */
        [[nodiscard]] uint32_t pending() const {
            std::shared_lock guard{_mutex};
            return std::count_if(
                _futures.begin(), _futures.end(), [](const Future &f) { return !f.isValid(); });
        }

        /**
         * @return vector of futures (thread safe)
         */
        [[nodiscard]] std::vector<Future> futures() const {
            std::shared_lock guard{_mutex};
            return _futures;
        }

        /**
         * @return all values, throws exception if any values are an exception
         */
        [[nodiscard]] std::vector<Container> getAll() const {
            std::vector<Future> all = futures();
            std::vector<Container> containers;
            containers.reserve(all.size());
            for(const auto &i : all) {
                containers.emplace_back(i.getValue());
            }
            return containers;
        }

        /**
         * Wait until all pending futures are valid
         */
        void waitAll() const {
            auto all = futures();
            for(const auto &i : all) {
                i.wait();
            }
        }

        /**
         * Wait until all pending futures are valid with timeout
         * @return true if no more pending futures
         */
        [[nodiscard]] bool waitAll(int32_t timeout) const {
            if(timeout < 0) {
                waitAll();
                return true;
            }
            auto all = futures();
            auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds{timeout};
            return std::all_of(all.begin(), all.end(), [limit](const Future &f) {
                auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    limit - std::chrono::steady_clock::now());
                auto maxWait = util::safeBoundPositive<int32_t>(remaining.count());
                return f.wait(maxWait);
            });
        }

        /**
         * Get future at given index (thread safe)
         * @param index
         * @return future at given index
         */
        [[nodiscard]] Future get(int32_t index) const {
            std::shared_lock guard{_mutex};
            return _futures.at(index);
        }

        /**
         * Get future at given index (thread safe)
         * @param index
         * @return future at given index
         */
        [[nodiscard]] Future operator[](int32_t index) const {
            return get(index);
        }

        /**
         * Get value at given index (thread safe)
         * @param index
         * @return value at given index
         */
        [[nodiscard]] Container getValue(int32_t index) const {
            return get(index).getValue();
        }
    };

    /**
     * Abstract async callback - used for asynchronous anonymous tasks
     */
    class AsyncCallback : public ObjHandle {

        /**
         * Templated dispatch allows for capturing multiple arguments to pass to a callback
         * routine. Note that this enables the same callback approach as std::invoke and std::thread
         * where a typical use is (callbackMethod, this) but acceptable also to do something like
         * (callbackMethod, this, extraArg1, extraArg2).
         * @tparam Callable Lambda, function pointer, method, etc.
         * @tparam Args Prefix arguments, particularly optional This
         */
        template<typename Callable, typename... Args>
        class AsyncDispatch : public CallbackManager::CaptureDispatch<Callable, Args...> {

        public:
            explicit AsyncDispatch(Callable callable, Args... args)
                : CallbackManager::CaptureDispatch<Callable, Args...>{
                    std::move(callable), std::move(args)...} {
                static_assert(std::is_invocable_v<Callable, Args...>);
            }
            [[nodiscard]] Symbol type() const override {
                return {"async"};
            }
            [[nodiscard]] CallbackManager::Delegate prepare(
                Symbol callbackType, ggapiDataLen size, void *data) const override {

                std::ignore =
                    this->template checkedStruct<ggapiAsyncCallbackData>(callbackType, size, data);
                return this->prepareWithArgs();
            }
        };

    public:
        constexpr AsyncCallback() noexcept = default;

        explicit AsyncCallback(const ObjHandle &other) : ObjHandle{other} {
        }

        /**
         * Create reference to a simple async task callback.
         */
        template<typename Callable, typename... Args>
        static AsyncCallback of(const Callable &callable, const Args &...args) {
            auto dispatch =
                std::make_unique<AsyncDispatch<std::decay_t<Callable>, std::decay_t<Args>...>>(
                    callable, args...);
            return CallbackManager::self().registerWithNucleus<AsyncCallback>(std::move(dispatch));
        }
    };

    /**
     * Abstract future callback - used where the parameter is a single Future
     */
    class FutureCallback : public ObjHandle {

        /**
         * Templated dispatch allows for capturing multiple arguments to pass to a callback
         * routine. Note that this enables the same callback approach as std::invoke and std::thread
         * where a typical use is (callbackMethod, this) but acceptable also to do something like
         * (callbackMethod, this, extraArg1, extraArg2).
         * @tparam Callable Lambda, function pointer, method, etc.
         * @tparam Args Prefix arguments, particularly optional This
         */
        template<typename Callable, typename... Args>
        class FutureDispatch : public CallbackManager::CaptureDispatch<Callable, Args...> {

        public:
            explicit FutureDispatch(Callable callable, Args... args)
                : CallbackManager::CaptureDispatch<Callable, Args...>{
                    std::move(callable), std::move(args)...} {
                static_assert(std::is_invocable_v<Callable, Args..., Future>);
            }
            [[nodiscard]] Symbol type() const override {
                return {"future"};
            }
            [[nodiscard]] CallbackManager::Delegate prepare(
                Symbol callbackType, ggapiDataLen size, void *data) const override {

                auto &cb =
                    this->template checkedStruct<ggapiFutureCallbackData>(callbackType, size, data);
                return this->prepareWithArgs(ObjHandle::of<Future>(cb.futureHandle));
            }
        };

    public:
        constexpr FutureCallback() noexcept = default;

        explicit FutureCallback(const ObjHandle &other) : ObjHandle{other} {
        }

        /**
         * Create reference to a simple async task callback.
         */
        template<typename Callable, typename... Args>
        static FutureCallback of(const Callable &callable, const Args &...args) {
            auto dispatch =
                std::make_unique<FutureDispatch<std::decay_t<Callable>, std::decay_t<Args>...>>(
                    callable, args...);
            return CallbackManager::self().registerWithNucleus<FutureCallback>(std::move(dispatch));
        }
    };

    inline void Future::whenValid(const FutureCallback &callback) const {
        callApiThrowError(::ggapiFutureAddCallback, getHandleId(), callback.getHandleId());
    }

    template<typename Callable, typename... Args>
    inline void Future::whenValid(const Callable &callable, Args &&...args) const {
        return whenValid(FutureCallback::of(callable, std::forward<Args>(args)...));
    }

    inline void sleep(uint32_t duration) {
        auto p{Promise::create()};
        std::ignore = p.wait(util::safeBoundPositive<int32_t>(duration));
    }

    inline void later(uint32_t delay, const AsyncCallback &callback) {
        callApiThrowError(::ggapiCallAsync, callback.getHandleId(), delay);
    }

    inline void async(const AsyncCallback &callback) {
        return later(0, callback);
    }

    template<typename Callable, typename... Args>
    inline void later(uint32_t delay, const Callable &callable, Args &&...args) {
        return later(delay, AsyncCallback::of(callable, std::forward<Args>(args)...));
    }

    template<typename Callable, typename... Args>
    inline void async(const Callable &callable, Args &&...args) {
        return async(AsyncCallback::of(callable, std::forward<Args>(args)...));
    }

    template<typename Callable, typename... Args>
    inline Promise &Promise::later(uint32_t delay, const Callable &callable, Args &&...args) {
        ggapi::later(delay, AsyncCallback::of(callable, std::forward<Args>(args)..., *this));
        return *this;
    }

    template<typename Callable, typename... Args>
    inline Promise &Promise::async(const Callable &callable, Args &&...args) {
        ggapi::async(AsyncCallback::of(callable, std::forward<Args>(args)..., *this));
        return *this;
    }

    template<typename Callable, typename... Args>
    inline Promise Future::andThen(const Callable &callable, Args &&...args) const {
        if(!*this) {
            return {};
        }
        Promise next = Promise::create();
        // TODO: an exception in FutureCallback will not get propagated to next
        // Maybe we define an AndThenCallback that will correctly handle exception?
        whenValid(FutureCallback::of(callable, std::forward<Args>(args)..., next));
        return next;
    }

    template<typename Func, typename... Args>
    inline Container Promise::fulfill(Func &&f, Args &&...args) {
        try {
            static_assert(std::is_invocable_r_v<ggapi::Container, Func, Args...>);
            Container c = std::invoke(std::forward<Func>(f), std::forward<Args>(args)...);
            setValue(c);
            return c;
        } catch(const ggapi::GgApiError &err) {
            setError(err); // Note, setError() can throw exception if Promise invalid
        } catch(const std::exception &exp) {
            setError(ggapi::GgApiError::of(exp));
        } catch(...) {
            setError(ggapi::GgApiError("Unknown error"));
        }
        return {};
    }

} // namespace ggapi
