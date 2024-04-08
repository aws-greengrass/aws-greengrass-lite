#pragma once
#include "data/handle_table.hpp"
#include "data/safe_handle.hpp"
#include "data/shared_struct.hpp"
#include "data/string_table.hpp"
#include "data/symbol_value_map.hpp"
#include "errors/error_base.hpp"
#include "scope/context.hpp"
#include "tasks/task.hpp"
#include <condition_variable>
#include <functional>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace pubsub {
    class Future;
    class Promise;

    class FutureBase : public data::TrackedObject {

    public:
        using BadCastError = errors::InvalidFutureError;
        explicit FutureBase(const scope::UsingContext &context);
        virtual std::shared_ptr<data::ContainerModelBase> getValue() const = 0;
        virtual bool isValid() const = 0;
        virtual bool waitUntil(const tasks::ExpireTime &when) const = 0;
        virtual std::shared_ptr<FutureBase> getFuture() = 0;
        virtual void addCallback(const std::shared_ptr<tasks::Callback> &callback) = 0;
    };

    /**
     * Completed Future that wraps an error
     */
    class ErrorFuture : public FutureBase {
        const errors::Error _error;

    public:
        explicit ErrorFuture(const scope::UsingContext &context, errors::Error error)
            : FutureBase(context), _error(std::move(error)) {
        }

        std::shared_ptr<data::ContainerModelBase> getValue() const override {
            throw _error;
        }
        bool isValid() const override {
            return true;
        }
        bool waitUntil(const tasks::ExpireTime &) const override {
            return true;
        }
        std::shared_ptr<FutureBase> getFuture() override {
            return ref<FutureBase>();
        }
        void addCallback(const std::shared_ptr<tasks::Callback> &callback) override;
    };

    /**
     * Completed Future that wraps a value
     */
    class ValueFuture : public FutureBase {
        const std::shared_ptr<data::ContainerModelBase> _value;

    public:
        explicit ValueFuture(
            const scope::UsingContext &context, std::shared_ptr<data::ContainerModelBase> value)
            : FutureBase(context), _value(std::move(value)) {
        }

        std::shared_ptr<data::ContainerModelBase> getValue() const override {
            return _value;
        }
        bool isValid() const override {
            return true;
        }
        bool waitUntil(const tasks::ExpireTime &) const override {
            return true;
        }
        std::shared_ptr<FutureBase> getFuture() override {
            return ref<FutureBase>();
        }
        void addCallback(const std::shared_ptr<tasks::Callback> &callback) override;
    };

    class Future : public FutureBase {
        const std::shared_ptr<Promise> _promise;
        friend class Promise;

    public:
        using BadCastError = errors::InvalidFutureError;
        explicit Future(
            const scope::UsingContext &context, const std::shared_ptr<Promise> &promise);
        std::shared_ptr<data::ContainerModelBase> getValue() const override;
        bool isValid() const override;
        bool waitUntil(const tasks::ExpireTime &when) const override;
        std::shared_ptr<FutureBase> getFuture() override;
        void addCallback(const std::shared_ptr<tasks::Callback> &callback) override;
    };

    class Promise : public FutureBase {

        std::variant<std::monostate, std::shared_ptr<data::ContainerModelBase>, errors::Error>
            _value;
        mutable std::shared_mutex _mutex;
        mutable std::condition_variable_any _fire;
        std::weak_ptr<Future> _future;
        std::vector<std::shared_ptr<tasks::Callback>> _callbacks;

        template<typename T>
        void setAndFire(const T &value);

        static std::shared_ptr<data::ContainerModelBase> handleValue(const std::monostate &);
        static std::shared_ptr<data::ContainerModelBase> handleValue(
            const std::shared_ptr<data::ContainerModelBase> &);
        static std::shared_ptr<data::ContainerModelBase> handleValue(const errors::Error &);

    public:
        using BadCastError = errors::InvalidPromiseError;
        explicit Promise(const scope::UsingContext &context);
        std::shared_ptr<FutureBase> getFuture() override;
        std::shared_ptr<data::ContainerModelBase> getValue() const override;
        void addCallback(const std::shared_ptr<tasks::Callback> &callback) override;
        bool isValid() const override;
        bool waitUntil(const tasks::ExpireTime &when) const override;
        void setValue(const std::shared_ptr<data::ContainerModelBase> &value);
        void setError(const errors::Error &error);
        void cancel();
    };

} // namespace pubsub
