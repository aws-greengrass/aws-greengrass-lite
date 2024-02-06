#pragma once
#include <chrono>
#include <cpp_api.hpp>
#include <util.hpp>

#include <filesystem>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "component_info.hpp"

namespace ipc {
    // implementation-defined process information
    template<class ProcessImpl>
    class AbstractProcess {
        friend class ProcessManager;
        friend ProcessImpl;

    private:
        ProcessImpl _impl;

    public:
        explicit AbstractProcess(ProcessImpl &&impl) noexcept : _impl(std::move(impl)) {
        }
        AbstractProcess(const AbstractProcess &) = delete;
        AbstractProcess(AbstractProcess &&) noexcept = default;
        AbstractProcess &operator=(AbstractProcess const &) = delete;
        AbstractProcess &operator=(AbstractProcess &&) noexcept = default;
        ~AbstractProcess() noexcept = default;

        [[nodiscard]] std::string_view getIdentifier() const noexcept {
            return _impl.getIdentifier();
        }

        // implementation-defined process termination function.
        // Implementations should attempt to signal-and-wait to close gracefully.
        // Otherwise, the process must be terminated immediately after a timeout, if non-zero
        void close(std::chrono::seconds timeout) {
            _impl.close(timeout);
        }

        int runToCompletion() {
            return _impl.waitFor(std::chrono::seconds{0});
        }

        [[nodiscard]] bool isRunning() const {
            return _impl.isRunning();
        }
    };

} // namespace ipc

#if defined(__unix__)
#include "linux/process.hpp"
namespace ipc {
    using Process = AbstractProcess<LinuxProcess>;
}
#else
#error "Unsupported platform"
#endif

namespace ipc {

    // // tracks processes executed by this plugin
    // class ProcessManagerImpl;
    // class ProcessManager {
    //     std::map<std::string, Process, std::less<>> processes;
    //     std::unique_ptr<ProcessManagerImpl> impl;

    //     std::shared_mutex m;

    // public:
    //     ProcessManager();

    //     // Processes output for each registered process
    //     // Effectively, blocks indefinitely; so, call from a separate thread
    //     // Implementation-defined, but must be thread-safe
    //     void ProcessOutput();

    //     void Register(Process &&p) {
    //         std::string key{p.getIdentifier()};
    //         std::unique_lock guard{m};
    //         processes.emplace(std::move(key), std::move(p));
    //     }

    //     std::optional<Process> Unregister(std::string_view identifier) {
    //         std::unique_lock guard{m};
    //         if(auto found = processes.find(identifier); found != processes.end()) {
    //             return std::move(processes.extract(found).mapped());
    //         } else {
    //             return {};
    //         }
    //     }
    // };

    // class for configuring and running an executable/shell command
    class Startable {
        friend class ProcessManager;

        static constexpr std::string_view PATH_ENVVAR = "PATH";

        friend class ComponentManager;
        std::string _command;
        std::vector<std::string> _args;
        std::unordered_map<std::string, std::optional<std::string>> _envs;
        std::optional<std::string> _user;
        std::optional<std::string> _group;
        std::optional<std::filesystem::path> _workingDir;
        ggapi::Channel _out;
        ggapi::Channel _err;

        // OS-specific start function
        // Starts execution the command with the arguments and environment provided
        Process start(std::string_view command, util::Span<char *> argv, util::Span<char *> envp);

    public:
        Process start() {
            if(_command.empty()) {
                throw std::invalid_argument("No command provided");
            }

            // Add SVCUID and IPC socket path
            // TODO: Get token and path from IPC plugin
            std::string token = "SVCUID=67TPFT1C5SNVYZ4T";
            std::string socket =
                "AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT=/tmp/gglite-ipc.socket";
            auto environment = GetEnvironment();
            // args and environment must each be a null-terminated array of pointers
            // Packed as follows: [ command | argv | nullptr | token | socket | envp | nullptr ]
            std::vector<char *> combinedArgvEnvp(
                1 + (_args.size() + 1) + 2 + (environment.size()), nullptr);
            combinedArgvEnvp.front() = _command.data();
            auto iter = std::transform(
                _args.begin(),
                _args.end(),
                std::next(combinedArgvEnvp.begin()),
                [](std::string &s) { return s.data(); });
            // skip over the first null-terminator; this marks the start of the envp array
            ++iter;
            auto argvSize = iter - combinedArgvEnvp.begin();

            *iter++ = token.data();
            *iter++ = socket.data();
            std::transform(environment.begin(), environment.end(), iter, [](std::string &s) {
                return s.data();
            });
            // TODO: include path variables
            combinedArgvEnvp.push_back(nullptr);

            auto envpSize = combinedArgvEnvp.size() - argvSize;

            return start(
                _command,
                util::Span{combinedArgvEnvp}.first(argvSize),
                util::Span{combinedArgvEnvp}.last(envpSize));
        }

        template<class String>
        std::enable_if_t<std::is_convertible_v<String, std::string>, Startable &> WithCommand(
            String &&command) noexcept {
            _command = std::string{std::forward<String>(command)};
            return *this;
        }

        Startable &WithArguments(std::vector<std::string> arguments) noexcept {
            _args = std::move(arguments);
            return *this;
        }

        Startable &AddArgument(std::string arg) {
            _args.emplace_back(std::move(arg));
            return *this;
        }

        std::vector<std::string> GetEnvironment() const {
            std::vector<std::string> flattened(_envs.size());
            std::transform(_envs.begin(), _envs.end(), flattened.begin(), [](auto &&pair) {
                if(pair.second.has_value()) {
                    return pair.first + "=" + *pair.second;
                } else {
                    return pair.first;
                }
            });
            return flattened;
        }

        Startable &WithEnvironment(
            std::unordered_map<std::string, std::optional<std::string>> environment) noexcept {
            _envs = std::move(environment);
            return *this;
        }

        Startable &AddEnvironment(
            std::string environment, std::optional<std::string> value = std::nullopt) {
            _envs.insert_or_assign(std::move(environment), std::move(value));
            return *this;
        }

        Startable &asUser(std::string username) noexcept {
            _user = std::move(username);
            return *this;
        }

        Startable &asGroup(std::string groupname) noexcept {
            _group = std::move(groupname);
            return *this;
        }

        Startable &WithWorkingDirectory(std::filesystem::path dir) noexcept {
            _workingDir = std::move(dir);
            return *this;
        }

        Startable &WithOutput(ggapi::Channel out) {
            _out = out;
            return *this;
        }

        Startable &WithError(ggapi::Channel error) {
            _err = error;
            return *this;
        }
    };
} // namespace ipc
