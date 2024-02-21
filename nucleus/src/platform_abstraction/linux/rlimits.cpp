#include "rlimits.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <system_error>

#include <cerrno>
#include <sys/resource.h>
#include <sys/select.h>

static constexpr rlimit make_rlimit(rlim_t limit) noexcept {
    return rlimit{limit, limit};
}

static std::error_code raiseLimitClosest(int resource, const struct rlimit &desired) noexcept {
    if(setrlimit(resource, &desired) >= 0) {
        return {};
    }

    if(errno != EPERM) {
        return {errno, std::generic_category()};
    }

    rlimit fixed{};
    if(getrlimit(resource, &fixed) < 0) {
        return {errno, std::generic_category()};
    }

    if(fixed.rlim_max == RLIM_INFINITY || fixed.rlim_max >= desired.rlim_max) {
        return {EPERM, std::generic_category()};
    }

    // clamp desired values to hard-cap.
    fixed.rlim_cur = std::min(fixed.rlim_max, desired.rlim_cur);
    fixed.rlim_max = std::min(fixed.rlim_max, desired.rlim_max);
    if(setrlimit(resource, &fixed) < 0) {
        return {errno, std::generic_category()};
    }
    return {};
}

std::error_code setFdLimit(int limit) noexcept {
    static constexpr int maxDefaultLimit =
        1024 * 1024; // most modern systems support 1024k descriptors
    static constexpr int minimumFiles = 3; // for stdin, stdout, stderr
    if(limit < 0) {
        // possible enhancement: to reduce the number of syscalls, try looking this up in
        // /proc/sys/fs/nr_open
        limit = maxDefaultLimit;
    }
    return raiseLimitClosest(RLIMIT_NOFILE, make_rlimit(std::max<rlim_t>(limit, minimumFiles)));
}

std::error_code resetFdLimit() noexcept {
    return setFdLimit(FD_SETSIZE);
}
