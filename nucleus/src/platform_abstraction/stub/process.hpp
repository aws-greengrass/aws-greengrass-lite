#pragma once

#include "platform_abstraction/abstract_process.hpp"

namespace ipc {

    namespace {
        namespace cr = std::chrono;
    }

    class StubProcess final : public AbstractProcess {
    public:
        StubProcess() noexcept = default;
        StubProcess(const StubProcess &) = delete;
        StubProcess(StubProcess &&) = delete;
        StubProcess &operator=(const StubProcess &) = delete;
        StubProcess &operator=(StubProcess &&) = delete;
        ~StubProcess() noexcept override = default;

        [[nodiscard]] bool isRunning() const noexcept override {
            return false;
        }

        void close(bool force) override{};
    };

    using Process = StubProcess;
} // namespace ipc
