#include "pipe.hpp"

#include <fcntl.h>
#include <unistd.h>

namespace ipc {
    std::pair<FileDescriptor, FileDescriptor> Pipe::MakePipe() {
        std::array<int, 2> fds{};
        if(pipe(fds.data()) == -1) {
            throw std::system_error(errno, std::generic_category());
        }
        // NOLINTBEGIN(*-vararg) fcntl or ioctl (both variadic)
        // is required to set non-blocking on MacOS
        for(auto fs : fds) {
            auto flags = fcntl(fs, F_GETFL, 0);
            if(flags == -1) {
                perror("fcntl");
                throw std::system_error(errno, std::generic_category());
            }
            auto error = fcntl(fs, F_SETFL, flags | O_NONBLOCK);
            if(error == -1) {
                perror("fcntl");
                throw std::system_error(errno, std::generic_category());
            }
        }
        // NOLINTEND(*-vararg)
        return std::make_pair(FileDescriptor{fds[0]}, FileDescriptor{fds[1]});
    }
} // namespace ipc
