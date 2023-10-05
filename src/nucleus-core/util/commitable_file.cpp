#include "commitable_file.h"

#include <utility>

namespace util {
    CommitableFile::CommitableFile(
        std::filesystem::path newPath,
        std::filesystem::path backupPath,
        std::filesystem::path targetPath
    )
        : _new(std::move(newPath)), _backup(std::move(backupPath)), _target(std::move(targetPath)) {
    }

    CommitableFile::CommitableFile(const std::filesystem::path &path)
        : CommitableFile(getNewFile(path), getBackupFile(path), path) {
    }

    CommitableFile &CommitableFile::begin() {
        if(!_stream.is_open()) {
            deleteNew();
            _didBegin = true; // make sure abandon gets called on close
            _stream.exceptions(std::ios::failbit | std::ios::badbit);
            _stream.open(_new, std::ios_base::trunc | std::ios_base::out);
        }
        return *this;
    }

    std::filesystem::path CommitableFile::getNewFile(const std::filesystem::path &path) {
        std::filesystem::path newPath{path};
        return newPath.replace_extension(path.extension().generic_string() + "+");
    }

    std::filesystem::path CommitableFile::getBackupFile(const std::filesystem::path &path) {
        std::filesystem::path newPath{path};
        return newPath.replace_extension(path.extension().generic_string() + "~");
    }

    CommitableFile &CommitableFile::abandon() {
        _stream.exceptions(std::ios::goodbit);
        if(_stream.is_open()) {
            _stream.close();
            if(_didBegin) {
                deleteNew();
                _didBegin = false;
            }
        }
        return *this;
    }

    CommitableFile &CommitableFile::deleteNew() {
        if(std::filesystem::exists(_new)) {
            std::error_code ec;
            std::filesystem::remove(_new, ec);
        }
        return *this;
    }

    CommitableFile &CommitableFile::deleteBackup() {
        if(std::filesystem::exists(_backup)) {
            std::error_code ec;
            std::filesystem::remove(_backup, ec);
        }
        return *this;
    }

    CommitableFile &CommitableFile::restoreBackup() {
        if(std::filesystem::exists(_backup)) {
            if(std::filesystem::exists(_target)) {
                std::error_code ec;
                std::filesystem::remove(_target, ec);
            }
            std::filesystem::rename(_backup, _target);
        }
        return *this;
    }

    CommitableFile &CommitableFile::moveTargetToBackup() {
        if(std::filesystem::exists(_target)) {
            if(std::filesystem::exists(_backup)) {
                std::error_code ec;
                std::filesystem::remove(_backup, ec);
            }
            std::filesystem::rename(_target, _backup);
        }
        return *this;
    }

    CommitableFile &CommitableFile::moveNewToTarget() {
        if(std::filesystem::exists(_new)) {
            moveTargetToBackup();
            std::filesystem::rename(_new, _target);
        }
        return *this;
    }

    CommitableFile &CommitableFile::commit() {
        if(_stream.is_open()) {
            _stream.flush();
            _stream.close();
        }
        if(_didBegin) {
            moveTargetToBackup();
            moveNewToTarget();
            _didBegin = false;
        }
        return *this;
    }

    CommitableFile::~CommitableFile() {
        abandon();
    }

} // namespace util
