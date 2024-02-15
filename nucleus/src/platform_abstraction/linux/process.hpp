#pragma once

#include "file_descriptor.hpp"
#include "permissions.hpp"
#include "platform_abstraction/abstract_process.hpp"

#include <chrono>
#include <stdexcept>
#include <system_error>

namespace ipc {

    namespace {
        namespace cr = std::chrono;
    }

    class LinuxProcess final : public AbstractProcess {
    public:
    private:
        FileDescriptor _pidfd;
        FileDescriptor _err;
        FileDescriptor _out;

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
            if(_onComplete) {
                _onComplete(returnCode);
            }
        }

        void close(bool force) override;
    };

    using Process = LinuxProcess;
} // namespace ipc
