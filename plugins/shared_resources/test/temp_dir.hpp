#pragma once
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>

namespace test {
    using namespace std::literals;

    /**
     * Generate temporary directory for testing
     */
    class TempDir final {
        std::filesystem::path _tempDir;

    public:
        // NOLINTNEXTLINE(*-err58-cpp) Exceptions in static
        inline static const auto PREFIX = "gg-lite-test-"s;
        inline static const auto MAX_ITERATIONS = 1000;

    private:
        static std::filesystem::path genPath() {
            auto tempdir = std::filesystem::temp_directory_path();
            std::random_device rd;
            std::mt19937 gen(rd());

            for(int i = 0; i < MAX_ITERATIONS; ++i) {
                auto num = gen();
                std::string tail = PREFIX + std::to_string(num);
                auto path = tempdir / tail;
                if(std::filesystem::create_directory(path)) {
                    return path;
                }
            }
            throw std::runtime_error("Tried too many times creating temporary directory");
        }

    public:
        TempDir() : _tempDir(genPath()) {
        }
        TempDir(const TempDir &) = delete;
        TempDir &operator=(const TempDir &) = delete;
        TempDir(TempDir &&) noexcept = default;
        TempDir &operator=(TempDir &&) noexcept = default;
        ~TempDir() {
            remove();
        }

        std::filesystem::path getDir() {
            return _tempDir;
        }

        void reset() {
            remove();
            _tempDir = genPath();
        }

        void remove() noexcept {
            if(_tempDir.empty()) {
                return;
            }

            std::filesystem::remove_all(_tempDir);
        }
    };

} // namespace test
