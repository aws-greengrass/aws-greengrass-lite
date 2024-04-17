#include "promise.hpp"
#include "errors/errors.hpp"
#include "tasks/expire_time.hpp"
#include "tasks/task_callbacks.hpp"
#include <exception>
#include <future>
#include <memory>

namespace tasks {
    static bool waitUntil(
        const tasks::ExpireTime &when,
        const std::shared_future<std::shared_ptr<data::ContainerModelBase>> &future) {
        switch(future.wait_until(when.toTimePoint())) {
            case std::future_status::timeout:
                return false;
            case std::future_status::deferred:
                future.wait();
                [[fallthrough]];
            case std::future_status::ready:
                return true;
        }
        return false;
    }
} // namespace tasks

namespace pubsub {
    struct PromiseCallbacks {
        std::shared_mutex m;
        std::vector<std::shared_ptr<tasks::Callback>> callbacks;
    };

    Future::~Future() noexcept = default;
    Promise::~Promise() noexcept = default;

    template<typename T>
    void Promise::setAndFire(const T &value) {
        std::unique_lock guard{_callbacks->m};
        try {
            if constexpr(std::is_same_v<std::exception_ptr, T>) {
                _promise.set_exception(value);
            } else {
                _promise.set_value(value);
            }
        } catch(const std::future_error &e) {
            switch(static_cast<std::future_errc>(e.code().value())) {
                case std::future_errc::promise_already_satisfied:
                    throw errors::PromiseDoubleWriteError{};
                case std::future_errc::no_state:
                case std::future_errc::future_already_retrieved:
                case std::future_errc::broken_promise:
                    throw errors::InvalidPromiseError{e.what()};
            }
        }

        if(_callbacks->callbacks.empty()) {
            return;
        }

        auto future = std::make_shared<Future>(context(), _callbacks, _future);
        auto callbacks = std::move(_callbacks->callbacks);
        _callbacks->callbacks.clear();
        guard.unlock();

        for(const auto &callback : callbacks) {
            if(callback) {
                callback->invokeFutureCallback(future);
            }
        }
    }

    Promise::Promise(const scope::UsingContext &context)
        : FutureBase(context), _callbacks(std::make_shared<PromiseCallbacks>()) {
    }

    std::shared_ptr<Future> Promise::getFuture() {
        std::shared_lock guard{_callbacks->m};
        return std::make_shared<Future>(context(), _callbacks, _future);
    }

    std::shared_ptr<data::ContainerModelBase> Promise::getValue() const {
        return _future.get();
    }

    void Promise::setValue(const std::shared_ptr<data::ContainerModelBase> &value) {
        setAndFire(value);
    }

    void Promise::setError(const std::exception_ptr &ex) {
        setAndFire(ex);
    }

    void Promise::cancel() {
        // TODO: specific error
        setError(std::make_exception_ptr(errors::PromiseCancelledError()));
    }

    void Promise::addCallback(const std::shared_ptr<tasks::Callback> &callback) {
        std::unique_lock guard{_callbacks->m};
        if(!isValid()) {
            _callbacks->callbacks.emplace_back(callback);
        } else {
            guard.unlock();
            callback->invokeFutureCallback(getFuture());
        }
    }

    bool Promise::isValid() const {
        using namespace std::chrono_literals;
        return _future.wait_for(0s) != std::future_status::timeout;
    }

    bool Promise::waitUntil(const tasks::ExpireTime &when) const {
        return tasks::waitUntil(when, _future);
    }

    FutureBase::FutureBase(const scope::UsingContext &context) : data::TrackedObject(context) {
    }

    Future::Future(
        const scope::UsingContext &context,
        std::shared_ptr<PromiseCallbacks> callbacks,
        std::shared_future<std::shared_ptr<data::ContainerModelBase>> future)
        : FutureBase(context), _callbacks{std::move(callbacks)}, _future{std::move(future)} {
    }

    std::shared_ptr<data::ContainerModelBase> Future::getValue() const {
        try {
            return _future.get();
        } catch(const std::future_error &e) {
            // none of these should be caught here, but translating them here for
            // completions-sake...
            throw errors::InvalidFutureError{e.what()};
        }
    }

    std::shared_ptr<Future> Future::getFuture() {
        return ref<Future>();
    }

    bool Future::isValid() const {
        using namespace std::chrono_literals;
        return _future.wait_for(0s) != std::future_status::timeout;
    }

    void Future::addCallback(const std::shared_ptr<tasks::Callback> &callback) {
        std::unique_lock guard{_callbacks->m};
        if(!isValid()) {
            _callbacks->callbacks.emplace_back(callback);
        } else {
            guard.unlock();
            callback->invokeFutureCallback(getFuture());
        }
    }

    bool Future::waitUntil(const tasks::ExpireTime &when) const {
        return tasks::waitUntil(when, _future);
    }
} // namespace pubsub
