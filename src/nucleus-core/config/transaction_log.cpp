#include "transaction_log.h"
#include "data/environment.h"
#include "json_helper.h"

namespace config {
    TlogWriter::TlogWriter(
        data::Environment &environment,
        const std::shared_ptr<Topics> &root,
        const std::filesystem::path &outputPath
    )
        : _environment(environment), _root(root), _tlogOutputPath(outputPath) {
        //_root->addWatcher(_watcher, WhatHappened::all);
    }

    TlogWriter::~TlogWriter() {
        close();
    }

    void TlogWriter::dump(
        data::Environment &environment,
        const std::shared_ptr<Topics> &root,
        const std::filesystem::path &outputPath
    ) {
        TlogWriter writer{environment, root, outputPath};
        writer.open(std::ios_base::out | std::ios_base::trunc);
        writer.writeAll();
        writer.close();
    }

    void TlogWriter::close() {
        std::unique_lock guard{_mutex};
        _watcher.reset();
        if(_writer) {
            _writer.flush();
            _writer.close();
        }
    }

    TlogWriter &TlogWriter::withWatcher(bool f) {
        std::unique_lock guard{_mutex};
        if(f) {
            if(!_watcher) {
                _watcher = std::make_shared<TlogWatcher>(*this);
            }
        } else {
            _watcher.reset();
        }
        return *this;
    }

    TlogWriter &TlogWriter::withAutoTruncate(bool f) {
        std::unique_lock guard{_mutex};
        _autoTruncate = f;
        return *this;
    }

    TlogWriter &TlogWriter::withMaxEntries(uint32_t maxEntries) {
        std::unique_lock guard{_mutex};
        _maxEntries = maxEntries;
        return *this;
    }

    TlogWriter &TlogWriter::flushImmediately() {
        std::unique_lock guard{_mutex};
        _writer.flush();
        return *this;
    }

    TlogWriter &TlogWriter::writeAll() {
        writeAll(_root);
        return *this;
    }

    std::filesystem::path TlogWriter::getPath() {
        std::unique_lock guard{_mutex};
        return _tlogOutputPath;
    }

    TlogWriter &TlogWriter::open(std::ios_base::openmode mode) {
        return open(getPath(), mode);
    }

    TlogWriter &TlogWriter::open(const std::filesystem::path &path, std::ios_base::openmode mode) {
        close();
        std::ofstream stream;
        stream.open(path, mode);
        std::unique_lock guard{_mutex};
        _writer = std::move(stream);
        return *this;
    }

    void TlogWriter::writeAll(const std::shared_ptr<Topics> &node) { // NOLINT(*-no-recursion)
        std::vector<Topic> leafs = node->getLeafs();
        for(auto i : leafs) {
            childChanged(i.getTopics(), i.getKeyOrd(), WhatHappened::childChanged);
        }
        leafs.clear();
        std::vector<std::shared_ptr<Topics>> subTopics = node->getInteriors();
        for(const auto &i : subTopics) {
            childChanged(i, data::StringOrd::nullHandle(), WhatHappened::interiorAdded);
            writeAll(i);
        }
        subTopics.clear();
    }

    void TlogWriter::childChanged(
        const std::shared_ptr<Topics> &topics, data::StringOrd key, WhatHappened changeType
    ) {
        std::unique_lock guard{_mutex};
        if(!_writer) {
            return; // closed
        }
        guard.unlock();
        if(!topics) {
            return;
        }
        if(topics->excludeTlog()) {
            return;
        }
        std::string keyString;

        TlogLine entry;
        if(key) {
            keyString = _environment.stringTable.getString(key);
            if(util::startsWith(keyString, "_")) {
                return;
            }
            entry.topicPath = topics->getKeyPath();
            Topic topic = topics->getChild(key);
            data::StringOrd nameOrd = topic.get().getNameOrd(); // retain case
            std::string nameString = _environment.stringTable.getString(nameOrd);
            entry.topicPath.push_back(nameString);
            entry.timestamp = topic.get().getModTime();
            entry.value = topic.get().slice();
            entry.action = changeType;
        } else {
            if((changeType & WhatHappened::childRemoved) != WhatHappened::never) {
                changeType = WhatHappened::removed;
            } else if((changeType & WhatHappened::interiorAdded) != WhatHappened::never) {
                changeType = WhatHappened::interiorAdded;
            } else if((changeType & WhatHappened::timestampUpdated) != WhatHappened::never) {
                changeType = WhatHappened::timestampUpdated;
            } else {
                return; // Other change types ignored
            }
            entry.action = changeType;
            entry.topicPath = topics->getKeyPath();
            entry.timestamp = topics->getModTime();
        }

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        entry.serialize(_environment, writer);

        guard.lock();
        if(!_writer) {
            return; // closed during above steps
        }
        _writer << buffer.GetString() << std::endl;
    }

    void TlogWatcher::changed(
        const std::shared_ptr<Topics> &topics, data::StringOrd key, WhatHappened changeType
    ) {
    }

    void TlogWatcher::childChanged(
        const std::shared_ptr<Topics> &topics, data::StringOrd key, WhatHappened changeType
    ) {
        _writer.childChanged(topics, key, changeType);
    }

    void TlogWatcher::initialized(
        const std::shared_ptr<Topics> &topics, data::StringOrd key, WhatHappened changeType
    ) {
    }
} // namespace config
