#pragma once
#include "data/tracked_object.hpp"
#include <exception>
#include <future>

namespace tasks {
    class ExpireTime;
    class Callback;
} // namespace tasks

namespace data {
    class ContainerModelBase;
}

namespace pubsub {
    class Future;
    class Promise;

    struct PromiseCallbacks;

    class FutureBase : public data::TrackedObject {

    public:
        using BadCastError = errors::InvalidFutureError;

        explicit FutureBase(const scope::UsingContext &context);
        virtual ~FutureBase() noexcept = default;
        FutureBase(const FutureBase &) = delete;
        FutureBase(FutureBase &&) = delete;
        FutureBase &operator=(const FutureBase &) = delete;
        FutureBase &operator=(FutureBase &&) = delete;

        virtual std::shared_ptr<data::ContainerModelBase> getValue() const = 0;
        virtual bool isValid() const = 0;
        virtual bool waitUntil(const tasks::ExpireTime &when) const = 0;
        virtual std::shared_ptr<Future> getFuture() = 0;
        virtual void addCallback(const std::shared_ptr<tasks::Callback> &callback) = 0;
    };

    class Future : public FutureBase {
        std::shared_ptr<PromiseCallbacks> _callbacks;
        std::shared_future<std::shared_ptr<data::ContainerModelBase>> _future;

    public:
        using BadCastError = errors::InvalidFutureError;

        Future(
            const scope::UsingContext &context,
            std::shared_ptr<PromiseCallbacks> callbacks,
            std::shared_future<std::shared_ptr<data::ContainerModelBase>> future);
        std::shared_ptr<Future> getFuture() override;
        ~Future() noexcept override;
        Future(const Future &) = delete;
        Future(Future &&) = delete;
        Future &operator=(const Future &) = delete;
        Future &operator=(Future &&) = delete;

        std::shared_ptr<data::ContainerModelBase> getValue() const override;
        bool isValid() const override;
        bool waitUntil(const tasks::ExpireTime &when) const override;
        void addCallback(const std::shared_ptr<tasks::Callback> &callback) override;
    };

    class Promise : public FutureBase {
        std::promise<std::shared_ptr<data::ContainerModelBase>> _promise;
        std::shared_future<std::shared_ptr<data::ContainerModelBase>> _future{
            _promise.get_future().share()};
        std::shared_ptr<PromiseCallbacks> _callbacks;

        template<typename T>
        void setAndFire(const T &value);

    public:
        using BadCastError = errors::InvalidPromiseError;

        explicit Promise(const scope::UsingContext &context);
        ~Promise() noexcept override;
        Promise(const Promise &) = delete;
        Promise(Promise &&) = delete;
        Promise &operator=(const Promise &) = delete;
        Promise &operator=(Promise &&) = delete;

        std::shared_ptr<Future> getFuture() override;
        std::shared_ptr<data::ContainerModelBase> getValue() const override;
        void addCallback(const std::shared_ptr<tasks::Callback> &callback) override;
        bool isValid() const override;
        bool waitUntil(const tasks::ExpireTime &when) const override;
        void setValue(const std::shared_ptr<data::ContainerModelBase> &value);
        void setError(const std::exception_ptr &ex);
        void cancel();
    };

} // namespace pubsub
