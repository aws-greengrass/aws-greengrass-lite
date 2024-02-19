#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

namespace ipc {
    // TODO: determine a list of environment variables to pass
    inline constexpr std::string_view PATH_ENVVAR = "PATH";

    // TODO: make optional
    inline std::string getEnviron(std::string_view variable) {
        // concurrent calls to getenv by itself does not introduce a data-race in C++11, as long as
        // functions modifying the host environment are not called.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        const char *env = std::getenv(variable.data());
        if(env == nullptr) {
            return {};
        }
        return {env};
    }
} // namespace ipc
