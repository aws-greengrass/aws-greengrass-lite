#include "promise.hpp"
#include "scope/context_full.hpp"
#include "tasks/task_callbacks.hpp"

namespace pubsub {

    std::shared_ptr<data::ContainerModelBase> Promise::handleValue(const std::monostate &) {
        throw errors::PromiseNotFulfilledError();
    }

    std::shared_ptr<data::ContainerModelBase> Promise::handleValue(
        const std::shared_ptr<data::ContainerModelBase> &value) {
        return value;
    }

    std::shared_ptr<data::ContainerModelBase> Promise::handleValue(const errors::Error &error) {
        throw error;
    }

    template<typename T>
    void Promise::setAndFire(const T &value) {
        std::vector<std::shared_ptr<tasks::Callback>> callbacks;
        std::unique_lock guard{_mutex};
        if(_value.index() != 0) {
            throw errors::PromiseDoubleWriteError();
        }
        _value = value;
        callbacks = _callbacks;
        _callbacks.clear();
        guard.unlock();
        auto f = getFuture();
        _fire.notify_all();
        for(auto &callback : callbacks) {
            if(callback) {
                callback->invokeFutureCallback(f);
            }
        }
    }

    Promise::Promise(const scope::UsingContext &context) : FutureBase(context) {
    }

    std::shared_ptr<FutureBase> Promise::getFuture() {
        std::unique_lock guard{_mutex};
        auto f = _future.lock();
        if(f) {
            return f;
        }
        f = std::make_shared<Future>(context(), ref<Promise>());
        _future = f;
        return f;
    }

    std::shared_ptr<data::ContainerModelBase> Promise::getValue() const {
        std::shared_lock guard{_mutex};
        return std::visit([](auto &&value) { return handleValue(value); }, _value);
    }

    void Promise::setValue(const std::shared_ptr<data::ContainerModelBase> &value) {
        setAndFire(value);
    }

    void Promise::setError(const errors::Error &error) {
        setAndFire(error);
    }

    void Promise::cancel() {
        // TODO: specific error
        setError(errors::PromiseCancelledError());
    }

    void Promise::addCallback(const std::shared_ptr<tasks::Callback> &callback) {
        std::unique_lock guard{_mutex};
        if(_value.index() == 0) {
            _callbacks.emplace_back(callback);
            return;
        }
        guard.unlock();
        callback->invokeFutureCallback(getFuture());
    }

    void ErrorFuture::addCallback(const std::shared_ptr<tasks::Callback> &callback) {
        callback->invokeFutureCallback(getFuture());
    }
    void ValueFuture::addCallback(const std::shared_ptr<tasks::Callback> &callback) {
        callback->invokeFutureCallback(getFuture());
    }

    bool Promise::isValid() const {
        std::shared_lock guard{_mutex};
        return _value.index() != 0;
    }

    bool Promise::waitUntil(const tasks::ExpireTime &when) const {
        std::shared_lock guard{_mutex};
        return _fire.wait_until(
            guard, when.toTimePoint(), [this]() { return _value.index() != 0; });
    }

    FutureBase::FutureBase(const scope::UsingContext &context) : data::TrackedObject(context) {
    }

    Future::Future(const scope::UsingContext &context, const std::shared_ptr<Promise> &promise)
        : FutureBase(context), _promise(promise) {
    }

    std::shared_ptr<data::ContainerModelBase> Future::getValue() const {
        return _promise->getValue();
    }

    bool Future::isValid() const {
        return _promise->isValid();
    }

    void Future::addCallback(const std::shared_ptr<tasks::Callback> &callback) {
        _promise->addCallback(callback);
    }

    bool Future::waitUntil(const tasks::ExpireTime &when) const {
        return _promise->waitUntil(when);
    }

    std::shared_ptr<FutureBase> Future::getFuture() {
        return ref<FutureBase>();
    }

} // namespace pubsub
