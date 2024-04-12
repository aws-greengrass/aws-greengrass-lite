#include <gg_pal/process.hpp>
#include <optional>
#include <string>

namespace gg_pal {

    Process::Process(
        std::string file,
        std::vector<std::string> args,
        std::filesystem::path workingDir,
        EnvironmentMap environment,
        std::optional<std::string> user,
        std::optional<std::string> group,
        OutputCallback stdoutCallback,
        OutputCallback stderrCallback,
        CompletionCallback onComplete)
        : _data{0} {
        throw std::logic_error{"Not implemented."};
    }

    Process::operator bool() const noexcept {
        return _data != 0;
    }

    void Process::stop() const {
        throw std::logic_error{"Not implemented."};
    }

    void Process::kill() const {
        throw std::logic_error{"Not implemented."};
    }

} // namespace gg_pal
