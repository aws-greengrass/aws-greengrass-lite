#pragma once
#include <util.hpp>

#include <system_error>
#include <utility>

class FileDescriptor {
    int _fd{-1};

public:
    explicit constexpr FileDescriptor(int fd) noexcept : _fd{fd} {
    }

    constexpr FileDescriptor() noexcept = default;

    // returns true if _fd is initialized to a valid file descriptor
    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return _fd >= 0;
    }

    FileDescriptor &operator=(const FileDescriptor &) = delete;
    FileDescriptor(const FileDescriptor &) = delete;

    FileDescriptor(FileDescriptor &&other) noexcept : _fd(other.release()) {
    }

    FileDescriptor &operator=(FileDescriptor &&other) noexcept {
        close();
        _fd = other.release();
        return *this;
    }

    ~FileDescriptor() noexcept {
        close();
    }

    // Releases ownership of the current file descriptor
    [[nodiscard]] int release() noexcept {
        return std::exchange(_fd, -1);
    }

    void close() noexcept {
        reset(-1);
    }

    // Close the current file descriptor and take ownership of a new one
    void reset(int newfd) noexcept;

    // Duplicates the current file descriptor onto an existing one (calls dup2)
    void duplicate(int fd) const;

    // Consumes all current readable output into a string
    [[nodiscard]] std::string readAll() const;

    [[nodiscard]] constexpr int get() const noexcept {
        return _fd;
    }

    [[nodiscard]] ssize_t write(util::Span<const char> buffer) const noexcept;

    [[nodiscard]] ssize_t read(util::Span<char> buffer) const noexcept;
};
