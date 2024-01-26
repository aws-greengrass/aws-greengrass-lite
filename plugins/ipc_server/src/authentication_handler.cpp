#include "authentication_handler.hpp"
#include <algorithm>

static constexpr int SEED = 123;

std::string AuthenticationHandler::generateAuthToken(const std::string &serviceName) {
    // SECURITY-TODO: Make random generation secure
    static constexpr size_t TOKEN_LENGTH = 16;
    std::string_view chars = "0123456789"
                             "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    thread_local std::mt19937 rng{SEED};
    auto dist = std::uniform_int_distribution{{}, chars.size() - 1};
    auto token = std::string(TOKEN_LENGTH, '\0');
    std::generate_n(token.begin(), TOKEN_LENGTH, [&]() { return chars[dist(rng)]; });
    _tokenMap.insert({token, serviceName});
    return token;
}

bool AuthenticationHandler::authenticateRequest(const std::string &authToken) const {
    if(_tokenMap.find(authToken) == _tokenMap.cend()) {
        return false;
    }
    if(auto serviceName = _tokenMap.at(authToken); serviceName.rfind("aws.greengrass", 0) != 0) {
        return false;
    }
    return true;
}
