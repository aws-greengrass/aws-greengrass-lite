#pragma once

#include "abstract_process.hpp"

namespace ipc {
    struct ProcessId {
        std::int64_t id;
    };
    class AbstractProcessManager {
    public:
        AbstractProcessManager() noexcept = default;
        AbstractProcessManager(const AbstractProcessManager &) = delete;
        AbstractProcessManager(AbstractProcessManager &&) = delete;
        AbstractProcessManager &operator=(const AbstractProcessManager &) = delete;
        AbstractProcessManager &operator=(AbstractProcessManager &&) = delete;

        virtual ~AbstractProcessManager() noexcept = default;
        virtual ProcessId registerProcess(std::unique_ptr<Process> proc) = 0;
        virtual void closeProcess(ProcessId pid) = 0;
    };
} // namespace ipc

#if defined(__linux__)
#include "linux/process_manager.hpp"
#else
#error "Unsupported platform"
#endif
