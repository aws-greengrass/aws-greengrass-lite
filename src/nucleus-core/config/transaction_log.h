#pragma once

#include <filesystem>

namespace config {
    class TlogReader {

    public:
        static bool validateTlog(const std::filesystem::path &tlogFile) {
            // TODO: missing code
            return false;
        }
    };

    class TlogWriter {

    };
}
