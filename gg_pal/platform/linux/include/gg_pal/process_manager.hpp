#pragma once

#include "file_descriptor.hpp"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <gg_pal/abstract_process.hpp>
#include <gg_pal/abstract_process_manager.hpp>
#include <gg_pal/process.hpp>
#include <list>
#include <mutex>
#include <thread>
#include <variant>

namespace ipc {

    class LinuxProcessManager final : public AbstractProcessManager {
    private:
        struct ProcessComplete {
            std::unique_ptr<Process> process;
        };
        struct ErrorLog {
            FileDescriptor fd;
            OutputCallback callback;
        };
        struct OutLog {
            FileDescriptor fd;
            OutputCallback callback;
        };
        struct InterruptEvent {};

        using ProcessEvent = std::variant<ProcessComplete, ErrorLog, OutLog, InterruptEvent>;
        std::atomic_bool _running{true};
        std::mutex _listMutex;
        std::list<ProcessEvent> _fds;

        FileDescriptor _epollFd{createEpoll()};
        FileDescriptor _eventfd{createEvent()};

        std::thread _thread{&LinuxProcessManager::workerThread, this};

        static FileDescriptor createEpoll();
        static FileDescriptor createEvent();

        void workerThread() noexcept;
        void addEvent(std::list<ProcessEvent> &eventList, ProcessEvent event);

    public:
        LinuxProcessManager() = default;
        LinuxProcessManager(const LinuxProcessManager &) = delete;
        LinuxProcessManager(LinuxProcessManager &&) = delete;
        LinuxProcessManager &operator=(const LinuxProcessManager &) = delete;
        LinuxProcessManager &operator=(LinuxProcessManager &&) = delete;
        ProcessId registerProcess(std::unique_ptr<Process> p) override;
        void closeProcess(ProcessId id, std::string reason) override;

        ~LinuxProcessManager() noexcept override;
    };

    using ProcessManager = LinuxProcessManager;
} // namespace ipc
