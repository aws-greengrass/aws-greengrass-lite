

// TODO: move into platform abstraction

#include <limits>
#include <string>

namespace ggpal {
    // implements a platform-specific, cryptographically-secure random_device
    class random_device {
    public:
        using result_type = unsigned int;

        random_device() noexcept = default;
        // compatibility with random_device, token ignored
        explicit random_device(const std::string &) noexcept : ggpal::random_device{} {
        }
        ~random_device() noexcept = default;
        random_device(const random_device &) = delete;
        random_device(random_device &&) = delete;
        random_device &operator=(const random_device &) = delete;
        random_device &operator=(random_device &&) = delete;

        // note: signature matches random_device::operator()
        result_type operator()() const;

        // NOLINTNEXTLINE(*-static) non-static definition conforms to C++11 standard
        [[nodiscard]] double entropy() const noexcept {
            return std::numeric_limits<result_type>::digits;
        }

        [[nodiscard]] static constexpr result_type min() noexcept {
            return std::numeric_limits<result_type>::min();
        }

        [[nodiscard]] static constexpr result_type max() noexcept {
            return std::numeric_limits<result_type>::max();
        }
    };
} // namespace ggpal
