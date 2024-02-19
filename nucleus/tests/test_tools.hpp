#pragma once
#include <filesystem>
#include <iostream>
#include <random>

namespace test {

    //
    // Access samples directory
    //
    inline std::filesystem::path samples() {
        std::array<std::filesystem::path, 2> alts = {"samples", "../samples"};
        for(const auto &alt : alts) {
            if(std::filesystem::exists(alt)) {
                return std::filesystem::absolute(alt);
            }
        }
        throw std::runtime_error("Cannot find samples directory");
    }

    //
    // Generate temporary directory for testing
    //
    class TempDir {
        std::filesystem::path _tempDir;

    private:
        std::filesystem::path genPath() {
            auto tempdir = std::filesystem::temp_directory_path();
            std::string prefix = "gg-lite-test-";
            std::random_device rd;
            std::mt19937 gen(rd());

            for(int i = 0; i < 1000; ++i) {
                auto num = gen();
                std::string tail = prefix + std::to_string(num);
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

        std::filesystem::path getDir() {
            return _tempDir;
        }

        void reset() {
            remove();
            _tempDir = genPath();
        }

        void remove() {
            if(_tempDir.empty()) {
                return;
            }

            std::error_code ec;
            std::filesystem::remove_all(_tempDir, ec);
            if(ec) {
                std::cerr << "Failed to clean up temporary directory" << std::endl;
            }
            _tempDir.clear();
        }

        ~TempDir() {
            remove();
        }
    };

} // namespace test
