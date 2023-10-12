#include "commitable_file.hpp"

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

    CommitableFile &CommitableFile::begin(std::ios_base::openmode mode) {
        if(!_stream.is_open()) {
            deleteNew();
            _mode = BEGIN_NEW;
            _stream.exceptions(std::ios::failbit | std::ios::badbit);
            _stream.open(_new, mode);
        }
        return *this;
    }

    CommitableFile &CommitableFile::append(std::ios_base::openmode mode) {
        if(!_stream.is_open()) {
            _mode = APPEND_EXISTING;
            _stream.exceptions(std::ios::failbit | std::ios::badbit);
            _stream.open(_target, mode);
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
            if(_mode == BEGIN_NEW) {
                deleteNew();
            }
            _mode = CLOSED;
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
        if(_mode == BEGIN_NEW) {
            moveTargetToBackup();
            moveNewToTarget();
        }
        _mode = CLOSED;
        return *this;
    }

    void CommitableFile::flush() {
        if(_stream.is_open()) {
            _stream.flush();
        }
    }

    CommitableFile::~CommitableFile() {
        switch(_mode) {
        case APPEND_EXISTING:
            commit();
            break;
        default:
            abandon();
            break;
        }
    }

    std::filesystem::path CommitableFile::getTargetFile() const {
        return _target;
    }

    std::filesystem::path CommitableFile::getNewFile() const {
        return _new;
    }

    std::filesystem::path CommitableFile::getBackupFile() const {
        return _backup;
    }

    bool CommitableFile::is_open() {
        return _stream.is_open();
    }

} // namespace util
