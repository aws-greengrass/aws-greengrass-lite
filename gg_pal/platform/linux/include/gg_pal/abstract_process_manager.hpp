#pragma once

#include "abstract_process.hpp"
#include <memory>

namespace ipc {
    struct ProcessId {
        std::int64_t pid;
        std::int64_t pidfd;
        explicit operator bool() const noexcept;
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
        virtual void closeProcess(ProcessId pid, std::string reason) = 0;
    };
} // namespace ipc

#include <gg_pal/process_manager.hpp>
