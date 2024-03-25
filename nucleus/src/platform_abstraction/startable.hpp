#pragma once
#include <chrono>
#include <iostream>
#include <optional>
#include <span.hpp>
#include <unordered_map>

#include "abstract_process.hpp"
#include "env.hpp"

#include <filesystem>

namespace ipc {
    using EnvironmentMap = std::unordered_map<std::string, std::optional<std::string>>;

    // class for configuring and running an executable/shell command
    class Startable {
        friend class ComponentManager;
        std::string _command;
        std::vector<std::string> _args;
        EnvironmentMap _envs;
        std::optional<std::string> _user;
        std::optional<std::string> _group;
        std::optional<std::filesystem::path> _workingDir;
        std::optional<OutputCallback> _outHandler;
        std::optional<OutputCallback> _errHandler;
        std::optional<CompletionCallback> _completeHandler;
        std::optional<std::chrono::steady_clock::time_point> _timeout;

        // OS-specific start function
        // Starts execution the command with the arguments and environment provided
        std::unique_ptr<Process> start(
            std::string_view command, util::Span<char *> argv, util::Span<char *> envp) const;

    public:
        std::unique_ptr<Process> start() const {
            if(_command.empty()) {
                throw std::invalid_argument("No command provided");
            }

            auto args = std::vector(1 + _args.size(), std::string{});
            args.front() = _command;
            std::copy(_args.begin(), _args.end(), std::next(args.begin()));

            auto environment = getEnvironment();

            // args and environment must each be a null-terminated array of pointers
            // Packed as follows: [ argv | nullptr | envp | nullptr ]
            std::vector<char *> combinedArgvEnvp;
            combinedArgvEnvp.reserve(2 + _args.size() + environment.size());

            const auto addRange = [&combinedArgvEnvp](auto &&container) -> size_t {
                auto offset = combinedArgvEnvp.size();
                std::transform(
                    container.begin(),
                    container.end(),
                    std::back_inserter(combinedArgvEnvp),
                    [](auto &s) -> char * { return s.data(); });
                combinedArgvEnvp.push_back(nullptr);
                return offset;
            };

            auto argv = addRange(args);
            auto envp = addRange(environment);
            return start(
                _command,
                util::Span{combinedArgvEnvp}.subspan(argv, args.size() + 1),
                util::Span{combinedArgvEnvp}.subspan(envp));
        }

        template<class StringLike>
        std::enable_if_t<std::is_convertible_v<StringLike, std::string>, Startable &> withCommand(
            StringLike &&command) noexcept(std::is_same_v<std::string, StringLike>) {
            _command = std::string{std::forward<StringLike>(command)};
            return *this;
        }

        Startable &withArguments(std::vector<std::string> arguments) noexcept {
            _args = std::move(arguments);
            return *this;
        }

        Startable &addArgument(std::string arg) {
            _args.emplace_back(std::move(arg));
            return *this;
        }

        std::vector<std::string> getEnvironment() const {
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

        Startable &withEnvironment(EnvironmentMap &&environment) noexcept {
            _envs = std::move(environment);
            return *this;
        }

        Startable &withEnvironment(const EnvironmentMap &environment) {
            _envs = environment;
            return *this;
        }

        template<class StringLike>
        Startable &addEnvironment(StringLike &&key, EnvironmentMap::mapped_type value) {
            _envs.insert_or_assign(
                static_cast<std::string>(std::forward<StringLike>(key)), std::move(value));
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

        Startable &withWorkingDirectory(std::filesystem::path dir) noexcept {
            _workingDir = std::move(dir);
            return *this;
        }

        Startable &withOutput(OutputCallback out) noexcept {
            _outHandler = std::move(out);
            return *this;
        }

        Startable &withError(OutputCallback error) noexcept {
            _errHandler = std::move(error);
            return *this;
        }

        Startable &withCompletion(CompletionCallback complete) noexcept {
            _completeHandler = std::move(complete);
            return *this;
        }
    };
} // namespace ipc
