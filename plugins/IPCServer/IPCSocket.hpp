#pragma once
#include <algorithm>
#include <bits/chrono.h>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

// TODO: Might need to break out usage into a private implemenation
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__) || defined(__CYGWIN__)
// TODO: does CYGWIN support all of these headers?
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#elif defined(_WIN32)
// TODO: Windows
#error IPC not supported yet
#else
#error IPC unsupported OS
#endif

#include <util.hpp>

static constexpr std::string_view IPC_LOG_TAG = "[IPCServer] ";

static inline std::error_code getLastError() noexcept {
    return std::make_error_code(std::errc{errno});
}

static inline std::error_code logLastError(
    std::string_view context = std::string_view{""}) noexcept {
    auto ec = getLastError();
    std::cerr << IPC_LOG_TAG << "ERROR " << context << ": " << ec << '\n';
    return ec;
}

static constexpr bool isNonblockingError(std::errc ec) noexcept {
    switch(ec) {
        case std::errc::operation_would_block:
        case std::errc::connection_already_in_progress:
            return true;
        default:
            return false;
    }
}

struct SocketSet;

class CloseableSocket {
protected:
    int _socketId{};

public:
    friend class SocketSet;

    // TODO: make private member and wrap all other usages
    [[nodiscard]] int getSocket() const noexcept {
        return _socketId;
    }

    constexpr CloseableSocket() noexcept = default;
    constexpr explicit CloseableSocket(int sd) noexcept : _socketId(sd) {
    }

    CloseableSocket(const CloseableSocket &) = delete;
    CloseableSocket &operator=(const CloseableSocket &) = delete;

    constexpr CloseableSocket(CloseableSocket &&other) noexcept
        : _socketId{std::exchange(other._socketId, 0)} {
    }

    CloseableSocket &operator=(CloseableSocket &&other) noexcept {
        if(this != &other) {
            this->~CloseableSocket();
            new(this) CloseableSocket{std::move(other)};
        }
        return *this;
    }

    friend void swap(CloseableSocket &lhs, CloseableSocket &rhs) noexcept {
        using std::swap;
        swap(lhs._socketId, rhs._socketId);
    }

    size_t read(util::Span<char> buffer, std::error_code &ec) const noexcept {
        if(auto received = recv(getSocket(), buffer.data(), buffer.size(), 0); received >= 0) {
            ec = {};
            return received;
        } else {
            ec = getLastError();
            return 0;
        }
    }

    size_t write(util::Span<char> buffer, std::error_code &ec) const noexcept {
        if(auto sent = send(getSocket(), buffer.data(), buffer.size(), 0); sent >= 0) {
            ec = {};
            return sent;
        } else {
            ec = getLastError();
            return 0;
        }
    }

    // NOLINTNEXTLINE(*-explicit-constructor)
    constexpr operator bool() const noexcept {
        return _socketId > 0;
    }

    virtual ~CloseableSocket() noexcept {
        if(*this) {
            if(close(_socketId) < 0) {
                logLastError("Socket close()");
            }
        }
    }
};

class Server {};

class Client {};

// TODO: remove client implementation
template<class S = Server>
class DomainSocket final : private CloseableSocket {
private:
    std::filesystem::path _path;
    static constexpr int GREENGRASS_IPC_MAX_CONN = 32;

    friend class SocketSet;

    // create non-blocking, selectable Unix domain listen socket
    static CloseableSocket open(std::filesystem::path &path) {
        CloseableSocket sock{socket(AF_UNIX, SOCK_STREAM, 0)};

        if(!sock) {
            throw std::system_error(logLastError("Socket open"));
        }

        auto _socketId = sock.getSocket();

        static constexpr int on = 1;
        if(setsockopt(_socketId, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
            throw std::system_error(logLastError("Socket setsockopt"));
        }

        std::array<char, sizeof(sockaddr_un)> buffer{};

        auto size = [&buffer, &path]() -> auto {
            sockaddr_un name{.sun_family = AF_LOCAL};
            auto nameSpan = util::Span(name.sun_path);
            auto pathStr = path.string();
            auto len = pathStr.copy(nameSpan.data(), nameSpan.size() - 1);
            auto size = offsetof(sockaddr_un, sun_path) + len;
            nameSpan[len] = '\0';

            // handle truncation
            // TODO: terminate instead?
            if(len < pathStr.size()) {
                pathStr.resize(len);
                path = std::move(pathStr);
            }

            // avoids pointer-aliasing between sockaddr and sockaddr_un
            std::memcpy(buffer.data(), &name, size);
            return size;
        }();

        if constexpr(std::is_same_v<S, Server>) {
            close(path);

            if(bind(
                   _socketId,
                   // NOLINTNEXTLINE (*reinterpret-cast)
                   reinterpret_cast<sockaddr *>(buffer.data()),
                   size)
               < 0) {
                throw std::system_error(logLastError("Socket bind()"));
            }

            if(listen(_socketId, GREENGRASS_IPC_MAX_CONN) < 0) {
                throw std::system_error(logLastError("Socket listen()"));
            }

            // make socket and all created sockets non-blocking
            // NOLINTNEXTLINE (*vararg-function)
            if(ioctl(_socketId, FIONBIO, &on) < 0) {
                throw std::system_error(logLastError("Socket ioctl (non-nonblocking)"));
            }
        } else {
            if(connect(
                   _socketId,
                   // NOLINTNEXTLINE (*reinterpret-cast)
                   reinterpret_cast<sockaddr *>(buffer.data()),
                   size)
               < 0) {
                throw std::system_error(logLastError("Socket connect()"));
            }
        }

        return sock;
    }

    static void close(const std::filesystem::path &path) noexcept {
        if constexpr(std::is_same_v<S, Server>) {
            if(!path.empty() && std::filesystem::exists(path)) {
                std::error_code ec{};
                std::filesystem::remove(path, ec);
                if(ec) {
                    std::cerr << IPC_LOG_TAG << "ERROR: Couldn't break pipe: " << ec << '\n';
                }
            }
        }
    }

public:
    using CloseableSocket::operator bool;
    using CloseableSocket::read;
    using CloseableSocket::write;

    DomainSocket(const DomainSocket &) = delete;
    DomainSocket &operator=(const DomainSocket &) = delete;
    DomainSocket(DomainSocket &&other) noexcept = default;
    DomainSocket &operator=(DomainSocket &&) noexcept = default;

    friend void swap(DomainSocket<S> &lhs, DomainSocket<S> &rhs) noexcept {
        using std::swap;
        swap(static_cast<CloseableSocket &>(lhs), static_cast<CloseableSocket &>(rhs));
        swap(lhs._path, rhs._path);
    }

    explicit DomainSocket(std::filesystem::path path)
        : CloseableSocket{open(path)}, _path(std::move(path)) {
    }

    CloseableSocket accept(std::error_code &ec) {
        if(int sd = ::accept(getSocket(), nullptr, nullptr); sd < 0) {
            ec = getLastError();
            return {};
        } else {
            ec = {};
            return CloseableSocket{sd};
        }
    }

    template<class OutputIt>
    OutputIt accept_range(OutputIt it, SocketSet &set, std::error_code &ec);

    ~DomainSocket() noexcept override {
        close(_path);
    }
};

struct SocketSet {
    using size_type = int;
    using value_type = int;
    // TODO: Windows
    fd_set _set{[] {
        fd_set set;
        FD_ZERO(&set);
        return set;
    }()};
    size_type _max{};

    // NOLINTNEXTLINE(bugprone-exception-escape)
    explicit SocketSet(const DomainSocket<Server> &sd) {
        static_assert(FD_SETSIZE > 0);
        insert(sd.getSocket());
    }

    constexpr SocketSet() noexcept = default;

    [[nodiscard]] inline bool contains(value_type sd) const noexcept {
        return FD_ISSET(sd, &_set);
    }

    template<class Socket>
    [[nodiscard]] inline bool contains(const Socket &socket) const noexcept {
        return contains(socket.getSocket());
    }

    [[nodiscard]] inline bool empty() const noexcept {
        return max() == size_type{0};
    }

    [[nodiscard]] inline size_type max() const noexcept {
        return _max;
    }

    [[nodiscard]] inline static size_type max_size() noexcept {
        return FD_SETSIZE;
    }

    void insert(value_type sd) {
        if(sd >= FD_SETSIZE) {
            throw std::out_of_range("Bad key");
        }
        if(contains(sd)) {
            throw std::invalid_argument("Key already exists");
        }
        FD_SET(sd, &_set);
        _max = std::max(sd, _max);
    }

    template<class Socket>
    void insert(const Socket &socket) {
        return insert(socket.getSocket());
    }

    void erase(value_type sd) {
        if(!contains(sd)) {
            throw std::invalid_argument("Key error");
        }

        FD_CLR(sd, &_set);

        --sd;
        while(sd > 0 && !contains(sd)) {
            --sd;
        }

        _max = sd;
    }

    template<class Socket>
    void erase(const Socket &socket) {
        erase(socket.getSocket());
    }

    void clear() noexcept {
        _max = 0;
        FD_ZERO(&_set);
    }

    fd_set *data() noexcept {
        return &_set;
    }

    template<class Timeout>
    [[nodiscard]] SocketSet select(const Timeout &timeout, std::error_code &ec) const noexcept {
        if(empty()) {
            return {};
        }

        using seconds = std::chrono::duration<decltype(timeval::tv_sec)>;
        using microseconds = std::chrono::duration<decltype(timeval::tv_usec), std::micro>;
        using namespace std::chrono_literals;
        using std::chrono::duration_cast;

        SocketSet other{*this};
        timeval t{
            .tv_sec = duration_cast<seconds>(timeout).count(),
            .tv_usec = (duration_cast<microseconds>(timeout) % 1s).count()};
        other._max = ::select(other.max() + 1, other.data(), nullptr, nullptr, &t);
        if(other._max < 0) {
            ec = getLastError();
            other.clear();
        } else {
            ec = {};
        }
        return other;
    }
};

template<class S>
template<class OutputIt>
OutputIt DomainSocket<S>::accept_range(OutputIt it, SocketSet &set, std::error_code &ec) {
    for(;; ++it) {
        auto socket = accept(ec);
        if(ec) {
            if(isNonblockingError(std::errc{ec.value()})) {
                ec = {};
            }
            break;
        }
        set.insert(socket);
        *it = std::move(socket);
    }
    return it;
}
