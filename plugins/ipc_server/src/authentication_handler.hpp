#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>

class Token {
private:
    std::string token;

public:
    Token() noexcept = default;
    explicit Token(std::string token) noexcept : token{std::move(token)} {
    }

    explicit operator std::string const &() const noexcept {
        return value();
    }

    [[nodiscard]] const std::string &value() const noexcept {
        return token;
    }

    [[nodiscard]] friend bool operator==(const Token &lhs, const Token &rhs) noexcept {
        return lhs.value() == rhs.value();
    };
};

template<>
struct std::hash<Token> {
    ::std::size_t operator()(const Token &token) const noexcept {
        return ::std::hash<::std::string>{}(token.value());
    }
};

// TODO: authorize service to perform action
class AuthenticationHandler {
    std::unordered_map<Token, std::string> _tokenMap;
    std::unordered_map<std::string, Token> _serviceMap;
    mutable std::shared_mutex _mutex;

public:
    Token generateAuthToken(std::string serviceName);
    bool authenticateRequest(const Token &authToken) const;
    void revokeService(const std::string &serviceName);
    void revokeToken(const Token &token);
};
