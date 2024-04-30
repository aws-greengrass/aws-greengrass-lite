#include "random_device.hpp"

// TODO: move into separate implementations inside platform abstraction
// TODO: rename ggpal to gg_pal and remove ggpal from dictionary

// C++17 standard __has_include preprocessor directive
#if __has_include(<unistd.h>) // detect POSIX platform
#include <unistd.h> // defines _POSIX_VERSION
#endif

#if defined(__MINGW32__)
#include <_mingw.h> // defines __MINGW64_VERSION_MAJOR
#endif

#if defined(_POSIX_VERSION) || defined(__CYGWIN__)
#define HAS_DEV_RANDOM 1

#include <fstream>

[[maybe_unused]] static std::streamsize devRandomBytes(char *data, std::streamsize size) {
    std::ifstream file;
    // disable input buffering https://en.cppreference.com/w/cpp/io/basic_filebuf/setbuf
    file.rdbuf()->pubsetbuf(nullptr, 0);
    file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    file.open("/dev/random", std::ios::binary | std::ios::in);
    file.read(data, size);
    return file.gcount();
}

#endif

#if __has_include(<sys/random.h>) // detect platform with getrandom() available

#include <array>
#include <atomic>
#include <cstring>
#include <iterator>
#include <sys/random.h> // getrandom()
#include <system_error>

ggpal::random_device::result_type ggpal::random_device::operator()() const {
    static std::atomic_bool getrandomAvailable = true;
    std::array<char, sizeof(result_type)> buffer{};
    auto first = buffer.data();
    auto last = buffer.data() + buffer.size();

    if(getrandomAvailable.load()) {
        while(first != last) {
#ifndef GRND_RANDOM // Darwin
            auto bytesWritten = getentropy(first, std::distance(first, last));
#else
            auto bytesWritten = getrandom(first, std::distance(first, last), GRND_RANDOM);
#endif
            if(bytesWritten < 0) {
                if(errno == EINTR) {
                    // syscall interrupted by signal
                    continue;
                } else if(errno == ENOSYS) {
                    // fall back to opening /dev/random
                    getrandomAvailable.store(false);
                    break;
                } else {
                    throw std::system_error{errno, std::generic_category()};
                }
            } else {
                std::advance(first, bytesWritten);
            }
        }
    }

    if(first != last) {
        auto bytesWritten = devRandomBytes(first, std::distance(first, last));
        std::advance(first, bytesWritten);
        if(first != last) {
            throw std::system_error{std::make_error_code(std::errc::io_error)};
        }
    }

    result_type value;
    std::memcpy(&value, buffer.data(), sizeof(value));
    return value;
}

// POSIX platform (includes Cygwin or any other platform which emulates /dev/random as a
// cryptographic entropy source)
#elif defined(HAS_DEV_RANDOM)

#include <array>
#include <cstring>
#include <limits>

ggpal::random_device::result_type ggpal::random_device::operator()() const {
    result_type result;
    static_assert(sizeof(result) <= std::numeric_limits<std::streamsize>::max());
    std::array<char, sizeof(result)> buffer{};
    devRandomBytes(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    std::memcpy(&result, buffer.data(), buffer.size());
    return result;
}

// Windows implementation (cl; Visual Studio, or MinGW-w64)
#elif defined(_MSC_FULL_VER) || defined(__MINGW64_VERSION_MAJOR)

#include <stdlib.h>
#include <system_error>

ggpal::random_device::result_type ggpal::random_device::operator()() const {
    static_assert(std::is_same_v<result_type, unsigned int>);
    result_type result;
    auto err = rand_s(&result);
    if(err != 0) {
        throw std::system_error{errno, std::generic_category()};
    }
    return result;
}

#endif
