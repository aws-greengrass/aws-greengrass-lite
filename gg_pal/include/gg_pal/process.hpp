#pragma once

#include <filesystem>
#include <functional>
#include <gg_pal/types.hpp>
#include <optional>
#include <span.hpp>
#include <string>
#include <vector>

namespace gg_pal {

    using EnvironmentMap = std::unordered_map<std::string, std::optional<std::string>>;
    using OutputCallback = std::function<void(util::Span<const char>)>;
    using CompletionCallback = std::function<void(int)>;

    class Process final {
        ProcessData _data{};

    public:
        Process(
            std::string file,
            std::vector<std::string> args,
            std::filesystem::path workingDir,
            EnvironmentMap environment,
            std::optional<std::string> user,
            std::optional<std::string> group,
            OutputCallback stdoutCallback,
            OutputCallback stderrCallback,
            CompletionCallback onComplete);

        explicit operator bool() const noexcept;

        void stop() const;
        void kill() const;
    };

} // namespace gg_pal
