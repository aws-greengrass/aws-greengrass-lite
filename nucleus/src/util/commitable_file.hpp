#pragma once
#include <filesystem>
#include <fstream>

namespace util {
    // GG-Interop: Partial implementation of GG-Java CommitableFile, focus on filename management.
    // Note that there are some nuanced differences between the two implementations.
    // In this implementation, begin() must be called, and commits do not happen unless commit()
    // is called.
    class CommitableFile {
        enum Mode { CLOSED, BEGIN_NEW, APPEND_EXISTING };

        std::filesystem::path _new;
        std::filesystem::path _target;
        std::filesystem::path _backup;
        std::ofstream _stream;
        Mode _mode{CLOSED};

    public:
        CommitableFile(const CommitableFile &) = delete;
        CommitableFile(CommitableFile &&) = default;
        CommitableFile &operator=(const CommitableFile &) = delete;
        CommitableFile &operator=(CommitableFile &&) = default;
        explicit CommitableFile(
            std::filesystem::path newPath,
            std::filesystem::path backupPath,
            std::filesystem::path targetPath
        );
        explicit CommitableFile(const std::filesystem::path &path);
        virtual ~CommitableFile();

        std::ofstream &getStream() {
            return _stream;
        }

        explicit operator std::ofstream &() {
            return getStream();
        }

        explicit operator std::filesystem::path() const {
            return getTargetFile();
        }

        CommitableFile &begin(
            std::ios_base::openmode mode = std::ios_base::trunc | std::ios_base::out
        );
        CommitableFile &append(
            std::ios_base::openmode mode = std::ios_base::app | std::ios_base::out
        );
        CommitableFile &commit();
        CommitableFile &abandon();
        CommitableFile &deleteNew();
        CommitableFile &deleteBackup();
        CommitableFile &restoreBackup();
        CommitableFile &moveTargetToBackup();
        CommitableFile &moveNewToTarget();
        static std::filesystem::path getNewFile(const std::filesystem::path &path);
        static std::filesystem::path getBackupFile(const std::filesystem::path &path);
        std::filesystem::path getTargetFile() const;
        std::filesystem::path getNewFile() const;
        std::filesystem::path getBackupFile() const;
        void flush();
        bool is_open();

        template<typename T>
        CommitableFile &operator<<(T v) {
            _stream << v;
            return *this;
        }
    };
} // namespace util
