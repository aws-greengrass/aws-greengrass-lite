#pragma once

#include "platform_abstraction/abstract_process.hpp"
#include "platform_abstraction/abstract_process_manager.hpp"

namespace ipc {

    class StubProcessManager final : public AbstractProcessManager {
    public:
        StubProcessManager() = default;
        StubProcessManager(const StubProcessManager &) = delete;
        StubProcessManager(StubProcessManager &&) = delete;
        StubProcessManager &operator=(const StubProcessManager &) = delete;
        StubProcessManager &operator=(StubProcessManager &&) = delete;
        ProcessId registerProcess(std::unique_ptr<Process> p) override {
            throw std::logic_error{"Not implememented."};
        }
        void closeProcess(ProcessId id, std::string reason) override {
            throw std::logic_error{"Not implememented."};
        };

        ~StubProcessManager() noexcept override = default;
    };

    using ProcessManager = StubProcessManager;

    inline ProcessId::operator bool() const noexcept {
        return false;
    };
} // namespace ipc
