#pragma once
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace ipc {
    // class for configuring and running an executable shell command
    class Startable {
        std::string _socketPath;
        std::string _authToken;
        std::string _command;
        std::vector<std::string> _args;
        std::unordered_map<std::string, std::optional<std::string>> _envs;
        std::optional<std::string> _user;
        std::optional<std::string> _group;

        // If true, the process spawned may outlive Nucleus
        bool _isDetached{true};

    public:
        Startable(std::string authToken, std::string socketPath)
            : _authToken(std::move(authToken)), _socketPath(std::move(socketPath)) {
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

        Startable &RunAs(std::string username) noexcept {
            _user = std::move(username);
            return *this;
        }

        Startable &RunWith(std::string username, std::string group) noexcept {
            _user = std::move(username);
            _group = std::move(group);
            return *this;
        }

        Startable &AsGroupedProcess() noexcept {
            _isDetached = false;
            return *this;
        }

        Startable &AsDetachedProcess() noexcept {
            _isDetached = true;
            return *this;
        }

        // OS-specific start function
        // Starts execution the command with the arguments and environment provided
        // A token and IPC socket path will be provided to the command
        void Start();
    };
} // namespace ipc
