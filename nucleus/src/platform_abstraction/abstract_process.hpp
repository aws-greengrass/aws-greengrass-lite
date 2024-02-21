#pragma once
#include <util.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <string>

namespace ipc {
    namespace {
        namespace cr = std::chrono;
    }

    using CompletionCallback = std::function<void(bool)>;
    using OutputCallback = std::function<void(util::Span<const char>)>;
    // implementation-defined process information
    class AbstractProcess {
    protected:
        friend class AbstractProcessManager;
        CompletionCallback _onComplete;
        OutputCallback _onOut;
        OutputCallback _onErr;

        cr::steady_clock::time_point timeout{cr::steady_clock::time_point::min()};

        friend class ProcessLogger;

    public:
        AbstractProcess() noexcept = default;

        template<class Period>
        explicit AbstractProcess(cr::duration<std::int64_t, Period> timeout) noexcept
            : AbstractProcess{cr::steady_clock::now() + timeout} {
        }

        explicit AbstractProcess(cr::steady_clock::time_point timeout) noexcept : timeout{timeout} {
        }

        AbstractProcess(const AbstractProcess &) = delete;
        AbstractProcess(AbstractProcess &&) = delete;
        AbstractProcess &operator=(const AbstractProcess &) = delete;
        AbstractProcess &operator=(AbstractProcess &&) = delete;
        virtual ~AbstractProcess() noexcept = default;

        // Implementation-defined process termination function.
        // Implementations should attempt to close gracefully if force is unset
        virtual void close(bool force) = 0;

        [[nodiscard]] constexpr auto getTimeout() const noexcept {
            return timeout;
        }

        [[nodiscard]] virtual bool isRunning() const noexcept = 0;
    };

} // namespace ipc

#if defined(__linux__)
#include "linux/process.hpp"
#else
#include "stub/process.hpp"
#endif
