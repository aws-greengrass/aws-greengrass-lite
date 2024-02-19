#include "pipe.hpp"

#include <fcntl.h>
#include <unistd.h>

namespace ipc {
    std::pair<FileDescriptor, FileDescriptor> Pipe::MakePipe() {
        std::array<int, 2> fds{};
        if(pipe2(fds.data(), O_NONBLOCK) == -1) {
            throw std::system_error(errno, std::generic_category());
        }
        return std::make_pair(FileDescriptor{fds[0]}, FileDescriptor{fds[1]});
    }
} // namespace ipc
