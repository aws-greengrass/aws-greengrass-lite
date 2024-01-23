#include <random>
#include <string>
#include <unordered_map>

class AuthenticationHandler {
    std::unordered_map<std::string, std::string> _tokenMap;

public:
    std::string generateAuthToken(const std::string &serviceName);
    bool authenticateRequest(const std::string &authToken) const;
};
