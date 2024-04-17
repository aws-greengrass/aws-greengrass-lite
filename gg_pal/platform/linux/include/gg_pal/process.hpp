#pragma once

#include "permissions.hpp"
#include <chrono>
#include <gg_pal/abstract_process.hpp>
#include <gg_pal/file_descriptor.hpp>
#include <iostream>
#include <stdexcept>
#include <system_error>

namespace ipc {

    namespace {
        namespace cr = std::chrono;
        constexpr int SIGKILLCODE = 9;
    } // namespace

    class LinuxProcess final : public AbstractProcess {
    public:
    private:
        FileDescriptor _pidfd;
        FileDescriptor _err;
        FileDescriptor _out;
        int _pid{};

        void terminate(bool force);

    public:
        LinuxProcess() noexcept = default;
        LinuxProcess(const LinuxProcess &) = delete;
        LinuxProcess(LinuxProcess &&) = delete;
        LinuxProcess &operator=(const LinuxProcess &) = delete;
        LinuxProcess &operator=(LinuxProcess &&) = delete;
        ~LinuxProcess() noexcept override = default;

        LinuxProcess &setPidFd(FileDescriptor &&pidfd) noexcept {
            _pidfd = std::move(pidfd);
            return *this;
        }

        LinuxProcess &setPid(int &pid) noexcept {
            _pid = std::move(pid);
            return *this;
        }

        int &getPid() & noexcept {
            return _pid;
        }

        FileDescriptor &getOut() noexcept {
            return _out;
        }

        FileDescriptor &getErr() noexcept {
            return _err;
        }

        LinuxProcess &setOut(FileDescriptor out) noexcept {
            _out = std::move(out);
            return *this;
        }

        LinuxProcess &setErr(FileDescriptor err) noexcept {
            _err = std::move(err);
            return *this;
        }

        LinuxProcess &setErrHandler(OutputCallback handler) noexcept {
            _onErr = std::move(handler);
            return *this;
        }

        LinuxProcess &setCompletionHandler(CompletionCallback handler) noexcept {
            _onComplete = std::move(handler);
            return *this;
        }

        LinuxProcess &setOutHandler(OutputCallback handler) noexcept {
            _onOut = std::move(handler);
            return *this;
        }

        LinuxProcess &setTimeout(cr::steady_clock::time_point timeout) noexcept {
            this->timeout = std::move(timeout);
            return *this;
        }

        OutputCallback &getErrorHandler() noexcept {
            return _onErr;
        }
        OutputCallback &getOutputHandler() noexcept {
            return _onOut;
        }

        [[nodiscard]] int queryReturnCode(std::error_code &ec) noexcept;

        [[nodiscard]] FileDescriptor &getProcessFd() & noexcept {
            return _pidfd;
        }

        [[nodiscard]] const FileDescriptor &getProcessFd() const & noexcept {
            return _pidfd;
        }

        [[nodiscard]] bool isRunning() const noexcept override;

        void complete(int returnCode) noexcept {
            switch(returnCode) {
                case(SIGKILLCODE):
                    std::cerr << "Process has been killed by the manager." << std::endl;
                    break;
                default:
                    break;
            }

            if(_onComplete) {
                _onComplete(returnCode);
            }
        }

        void close(bool force) override;
    };

    using Process = LinuxProcess;
} // namespace ipc
