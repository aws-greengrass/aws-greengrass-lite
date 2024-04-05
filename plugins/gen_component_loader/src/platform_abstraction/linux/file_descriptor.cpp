
#include "file_descriptor.hpp"
#include "error.hpp"
#include <array>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

void FileDescriptor::reset(int newFd) noexcept {
    if(int old = std::exchange(_fd, newFd); old != -1) {
        if(::close(old) == -1) {
            perror("close");
        }
    }
}

void FileDescriptor::duplicate(int fd) const {
    if(dup2(_fd, fd) == -1) {
        throw std::system_error(errno, std::generic_category());
    }
}

std::string FileDescriptor::readAll() const {
    if(!*this) {
        return {};
    }

    std::string output;
    static constexpr size_t defaultBufferSize = 0xFFF;
    std::array<char, defaultBufferSize> buffer{};

    for(;;) {
        ssize_t bytesRead = read(buffer);
        if(bytesRead == -1) {
            if(ipc::isNonBlockingError(errno)) {
                break;
            }
            perror("read");
            throw std::system_error(errno, std::generic_category());
        }
        output.append(buffer.data(), bytesRead);
        if(static_cast<size_t>(bytesRead) < buffer.size()) {
            break;
        }
    }

    return output;
}

ssize_t FileDescriptor::write(util::Span<const char> buffer) const noexcept {
    return ::write(_fd, buffer.data(), buffer.size_bytes());
}

ssize_t FileDescriptor::read(util::Span<char> buffer) const noexcept {
    return ::read(_fd, buffer.data(), buffer.size_bytes());
}
