#pragma once
#include "errors/errors.hpp"
#include <memory>
#include <optional>
#include <utility>

namespace scope {
    class Context;
    class LazyContext;
    class ThreadContextContainer;
    class PerThreadContext;
    class NucleusCallScopeContext;
    class CallScope;

    using ContextRef = std::shared_ptr<Context>;
    using WeakContext = std::weak_ptr<Context>;
    using PerThreadContextRef = std::shared_ptr<PerThreadContext>;

    /**
     * Typically used to decorate a block of code using 'visit' or 'safeVisit', where context is set
     * for that block of code.
     */
    class UsingContext {
        ContextRef _context;

    public:
        // NOLINTNEXTLINE(*-explicit-constructor)
        UsingContext(ContextRef context) noexcept : _context(std::move(context)) {
        }
        // NOLINTNEXTLINE(*-explicit-constructor)
        UsingContext(const WeakContext &context) noexcept : _context(context.lock()) {
        }
        UsingContext() noexcept; // copies context of environment
        UsingContext(const UsingContext &other) noexcept = default;
        UsingContext(UsingContext &&other) noexcept = default;
        UsingContext &operator=(const UsingContext &other) noexcept = default;
        UsingContext &operator=(UsingContext &&other) noexcept = default;
        ~UsingContext() noexcept = default;

        [[nodiscard]] Context *get() const noexcept {
            return _context.get();
        }
        [[nodiscard]] Context *checked() const {
            auto ref = get();
            if(!ref) {
                throw errors::InvalidContextError();
            }
            return ref;
        }
        [[nodiscard]] bool hasValue() const noexcept {
            return _context.operator bool();
        }
        Context *operator->() const {
            return checked();
        }
        Context &operator*() const {
            return *checked();
        }
        explicit operator bool() const {
            return hasValue();
        }
        // NOLINTNEXTLINE(*-explicit-constructor)
        operator ContextRef() const noexcept {
            return _context;
        }
        // NOLINTNEXTLINE(*-explicit-constructor)
        operator WeakContext() const noexcept {
            return _context;
        }
    };

    /**
     * Mix-in class for classes that need to track associated context
     */
    class UsesContext {
    private:
        WeakContext _context;

    public:
        UsesContext() = default;
        explicit UsesContext(const UsingContext &context) noexcept : _context(context) {
        }

        [[nodiscard]] UsingContext context() const noexcept {
            return {_context};
        }
    };

} // namespace scope
