#include <gg_pal/abstract_process.hpp>
#include <gg_pal/startable.hpp>

namespace ipc {

    std::unique_ptr<Process> Startable::start(
        std::string_view command, util::Span<char *> argv, util::Span<char *> envp) const {
        throw std::logic_error{"Not implemented."};
    }

} // namespace ipc
