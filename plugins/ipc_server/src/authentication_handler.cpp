#include "authentication_handler.hpp"
#include "random_device.hpp"
#include <algorithm>
#include <mutex>
#include <random>

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

bool AuthenticationHandler::authenticateRequest(const Token &authToken) const {
    std::shared_lock guard{_mutex};
    auto found = _tokenMap.find(authToken);
    if(found == _tokenMap.cend()) {
        return false;
    }
    const auto &serviceName = found->second;
    if(serviceName.rfind("aws.greengrass", 0) != 0) {
        return false;
    }
    return true;
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
