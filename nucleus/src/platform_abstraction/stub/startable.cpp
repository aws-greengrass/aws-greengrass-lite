#include "platform_abstraction/startable.hpp"
#include "platform_abstraction/abstract_process.hpp"

namespace ipc {

    std::unique_ptr<Process> Startable::start(
        std::string_view command, util::Span<char *> argv, util::Span<char *> envp) const {
        throw std::logic_error{"Not implememented."};
    }

} // namespace ipc
