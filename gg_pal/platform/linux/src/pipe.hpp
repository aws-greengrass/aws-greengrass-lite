#pragma once

#include <gg_pal/file_descriptor.hpp>
#include <utility>

namespace ipc {
    class Pipe {
        FileDescriptor _output;
        FileDescriptor _input;

        explicit Pipe(std::pair<FileDescriptor, FileDescriptor> &&fd) noexcept
            : _output{std::move(fd.first)}, _input{std::move(fd.second)} {
        }

        static std::pair<FileDescriptor, FileDescriptor> MakePipe();

    public:
        Pipe() : Pipe{MakePipe()} {
        }

        FileDescriptor &input() & noexcept {
            return _input;
        }

        FileDescriptor &output() & noexcept {
            return _output;
        }
    };
} // namespace ipc
