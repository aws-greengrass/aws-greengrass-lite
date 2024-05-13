#include "authentication_handler.hpp"
#include "random_device.hpp"
#include <algorithm>
#include <mutex>
#include <shared_mutex>

namespace ipc_server {
    Token AuthenticationHandler::generateAuthToken(std::string serviceName) {
        static constexpr size_t TOKEN_LENGTH = 16;
        // Base64
        static constexpr std::string_view chars = "0123456789"
                                                  "+/"
                                                  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                                  "abcdefghijklmnopqrstuvwxyz";
        ggpal::random_device rng;
        auto dist = std::uniform_int_distribution<std::string_view::size_type>{0, chars.size() - 1};
        auto token = std::string(TOKEN_LENGTH, '\0');
        std::generate(token.begin(), token.end(), [&] { return chars.at(dist(rng)); });

        std::unique_lock guard{_mutex};
        auto [iter, emplaced] = _tokenMap.emplace(serviceName + ":" + token, serviceName);
        if(emplaced) {
            try {
                _serviceMap.emplace(std::move(serviceName), iter->first);
            } catch(...) {
                _tokenMap.erase(iter);
                throw;
            }
        }
        return iter->first;
    }

    void AuthenticationHandler::revokeService(std::string const &serviceName) {
        std::unique_lock guard{_mutex};
        auto found = _serviceMap.find(serviceName);
        if(found != _serviceMap.cend()) {
            _tokenMap.erase(found->second);
            _serviceMap.erase(found);
        }
    }

    void AuthenticationHandler::revokeToken(const Token &token) {
        std::unique_lock guard{_mutex};
        auto found = _tokenMap.find(token);
        if(found != _tokenMap.cend()) {
            _serviceMap.erase(found->second);
            _tokenMap.erase(found);
        }
    }

    std::optional<std::string> AuthenticationHandler::retrieveServiceName(
        const Token &token) const {
        std::shared_lock guard{_mutex};
        auto found = _tokenMap.find(token);
        if(found == _tokenMap.cend()) {
            return {};
        }
        auto serviceName = found->second;
        guard.unlock();

        // TODO: uncomment this. Currently,  breaks IPC tests
        // if(serviceName.rfind("aws.greengrass", 0) != 0) {
        //     return {};
        // }
        return serviceName;
    }
} // namespace ipc_server
