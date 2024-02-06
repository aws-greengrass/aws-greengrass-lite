
#include "file_descriptor.hpp"

#include <stdexcept>
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

ssize_t FileDescriptor::write(util::Span<const char> buffer) noexcept {
    return ::write(_fd, util::as_bytes(buffer).data(), buffer.size_bytes());
}

ssize_t FileDescriptor::read(util::Span<char> buffer) noexcept {
    return ::read(_fd, util::as_writeable_bytes(buffer).data(), buffer.size_bytes());
}
