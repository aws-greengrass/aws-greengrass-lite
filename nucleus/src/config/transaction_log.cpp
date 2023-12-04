#include "transaction_log.hpp"
#include "scope/context_full.hpp"
#include "tlog_json.hpp"
#include <iostream>
#include <iterator>

namespace config {
    TlogWriter::TlogWriter(
        const std::shared_ptr<scope::Context> &context,
        const std::shared_ptr<Topics> &root,
        const std::filesystem::path &outputPath)
        : _context(context), _root(root), _tlogFile(outputPath) {
    }

    TlogWriter &TlogWriter::dump() {
        startNew();
        writeAll();
        commit();
        return *this;
    }

    void TlogWriter::commit() {
        std::unique_lock guard{_mutex};
        _watcher.reset();
        _tlogFile.commit();
    }

    void TlogWriter::abandon() {
        std::unique_lock guard{_mutex};
        _watcher.reset();
        _tlogFile.abandon();
    }

    TlogWriter &TlogWriter::withWatcher(bool f) {
        std::unique_lock guard{_mutex};
        if(f) {
            if(!_watcher) {
                _watcher = std::make_shared<TlogWatcher>(*this);
            }
            _root->addWatcher(_watcher, WhatHappened::all);
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

    TlogWriter &TlogWriter::flushImmediately(bool f) {
        std::unique_lock guard{_mutex};
        _flushImmediately = f;
        if(f) {
            _tlogFile.flush();
        }
        return *this;
    }

    TlogWriter &TlogWriter::writeAll() {
        writeAll(_root);
        return *this;
    }

    std::filesystem::path TlogWriter::getPath() const {
        std::unique_lock guard{_mutex};
        return _tlogFile.getTargetFile();
    }

    TlogWriter &TlogWriter::startNew() {
        std::unique_lock guard{_mutex};
        _tlogFile.begin();
        return *this;
    }

    TlogWriter &TlogWriter::append() {
        std::unique_lock guard{_mutex};
        _tlogFile.append();
        return *this;
    }

    void TlogWriter::writeAll(const std::shared_ptr<Topics> &node) { // NOLINT(*-no-recursion)
        std::vector<Topic> leafs = node->getLeafs();
        for(const auto &i : leafs) {
            childChanged(i, WhatHappened::childChanged);
        }
        leafs.clear();
        std::vector<std::shared_ptr<Topics>> subTopics = node->getInteriors();
        for(const auto &i : subTopics) {
            childChanged(*i, WhatHappened::interiorAdded);
            writeAll(i);
        }
        subTopics.clear();
    }

    void TlogWriter::childChanged(const ConfigNode &node, WhatHappened changeType) {
        if(node.excludeTlog()) {
            return;
        }
        auto *nodeAsTopic = dynamic_cast<const Topic *>(&node);
        TlogLine tlogline;
        tlogline.topicPath = node.getKeyPath();
        tlogline.timestamp = node.getModTime();
        if((changeType & (WhatHappened::changed | WhatHappened::childChanged))
               != WhatHappened::never
           && nodeAsTopic != nullptr) {
            tlogline.value = nodeAsTopic->slice();
            tlogline.action = WhatHappened::changed;
        } else {
            if((changeType & WhatHappened::childRemoved) != WhatHappened::never) {
                tlogline.action = WhatHappened::removed;
            } else if((changeType & WhatHappened::interiorAdded) != WhatHappened::never) {
                tlogline.action = WhatHappened::interiorAdded;
            } else if((changeType & WhatHappened::timestampUpdated) != WhatHappened::never) {
                tlogline.action = WhatHappened::timestampUpdated;
            } else {
                return; // Other change types ignored
            }
        }

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        tlogline.serialize(_context.lock(), writer);

        std::unique_lock guard{_mutex};
        if(!_tlogFile.is_open()) {
            return;
        }
        _tlogFile << buffer.GetString() << "\n";
        if(_flushImmediately) {
            _tlogFile.flush();
        }
        uint32_t currentCount = ++_count;
        // TODO: insert auto-truncate logic from ConfigurationWriter::childChanged
    }

    std::filesystem::path TlogWriter::getOldTlogPath(const std::filesystem::path &path) {
        std::filesystem::path copy{path};
        return copy.replace_extension(".old");
    }

    void TlogWatcher::changed(
        const std::shared_ptr<Topics> &topics, data::Symbol key, WhatHappened changeType) {
    }

    void TlogWatcher::childChanged(
        const std::shared_ptr<Topics> &topics, data::Symbol key, WhatHappened changeType) {
        if(key) {
            _writer.childChanged(*topics->getNode(key), changeType);
        } else {
            _writer.childChanged(*topics, changeType);
        }
    }

    void TlogWatcher::initialized(
        const std::shared_ptr<Topics> &topics, data::Symbol key, WhatHappened changeType) {
    }

    bool TlogReader::handleTlogTornWrite(
        const std::shared_ptr<scope::Context> &context, const std::filesystem::path &tlogFile) {
        uintmax_t fileSize = 0;
        uintmax_t lastValid = 0;
        try {
            if(!std::filesystem::exists(tlogFile)) {
                // TODO: Log
                std::cerr << "Transaction log file does not exist at given path: " << tlogFile
                          << std::endl;
                return false;
            }
            fileSize = std::filesystem::file_size(tlogFile);
            if(fileSize == 0) {
                // TODO: Log
                std::cerr << "Transaction log is zero-length at given path: " << tlogFile
                          << std::endl;
                return false;
            }
            // GG-Interop:
            // The GG-Java validation takes a hammer approach to validation by parsing the
            // entire file to handle torn-write scenarios (where the last write did not
            // complete). The pattern here follows a self-repair approach, per also used in
            // StreamManager
            //
            // If the host crashes while writing to Tlog (which is an append-log), then
            // the last record may not be entirely written. In theory, we can look for
            // "}<whitespace>" as heuristic, but safer approach is to pre-scan to find first
            // invalid JSON structure. To be better repairing, if the majority of the file
            // is valid, then the file is truncated and accepted.
            //
            std::ifstream stream;
            stream.open(tlogFile, std::ios_base::in);
            if(!stream) {
                // TODO: Log
                std::cerr << "Failed to open Transaction log: " << tlogFile << std::endl;
                return false;
            }
            while(!stream.eof()) {
                conv::JsonReader reader(context);
                reader.push(std::make_unique<conv::JsonStructValidator>(reader, false));
                rapidjson::ParseResult result = reader.read(stream);
                if(result) {
                    lastValid = stream.tellg(); // update watermark
                } else {
                    break;
                }
            }
            stream.clear();
            // whitespace is permitted after last valid position
            stream.seekg(
                lastValid, // NOLINT(*-narrowing-conversions)
                std::ifstream::beg);
            std::string_view whitespace{" \t\n\v\f\r"};
            while(!stream.eof()) {
                char c;
                if(!stream.get(c)) {
                    break;
                }
                if(whitespace.find(c) == std::string_view::npos) {
                    break;
                }
                ++lastValid;
            }
            if(lastValid == fileSize) {
                return true; // typical case where entire file is valid
            }
            if(lastValid == 0) {
                // TODO: Log
                std::cerr << "Entire Transaction log is invalid: " << tlogFile << std::endl;
                return false;
            }
            if((fileSize - lastValid) > (fileSize / 4)) {
                // TODO: Log
                std::cerr << "Transaction log corrupted / torn-write - would truncate too small: "
                          << tlogFile << std::endl;
                return false;
            } else {
                // TODO: make a fresh copy of TLOG and keep the apparently corrupt TLOG
                std::filesystem::resize_file(tlogFile, lastValid);
                // TODO: Log
                std::cerr << "Transaction log truncated to last valid entry: " << tlogFile
                          << std::endl;
                return true;
            }
        } catch(std::ofstream::failure &e) {
            // TODO: Log
            std::cerr << "Unable to read Tlog\n";
            return false;
        }
    }

    void TlogReader::mergeTlogInto(
        const std::shared_ptr<scope::Context> &context,
        const std::shared_ptr<Topics> &root,
        std::ifstream &stream,
        bool forceTimestamp,
        const std::function<bool(ConfigNode &)> &mergeCondition,
        ConfigurationMode configurationMode) {
        while(!stream.eof()) {
            TlogLine tlogLine = TlogLine::readRecord(context, stream);
            if(tlogLine.action == WhatHappened::never) {
                continue;
            }
            if(tlogLine.action == WhatHappened::changed) {
                Topic targetTopic{root->lookup(tlogLine.topicPath)};
                if(!mergeCondition(targetTopic)) {
                    continue;
                }
                if(configurationMode == ConfigurationMode::WITH_VALUES) {
                    targetTopic.withNewerValue(
                        tlogLine.timestamp, tlogLine.value.get(), forceTimestamp);
                }
            } else if(tlogLine.action == WhatHappened::removed) {
                std::shared_ptr<ConfigNode> node{root->getNode(tlogLine.topicPath)};
                if(!node) {
                    continue;
                }
                if(forceTimestamp) {
                    // always remove
                    node->remove();
                } else {
                    // conditional remove
                    node->remove(tlogLine.timestamp);
                }
            } else if(tlogLine.action == WhatHappened::timestampUpdated) {
                Topic targetTopic{root->lookup(tlogLine.topicPath)};
                targetTopic.withNewerModTime(tlogLine.timestamp);
            } else if(tlogLine.action == WhatHappened::interiorAdded) {
                (void) root->lookupTopics(tlogLine.topicPath);
            }
        }
    }

    void TlogReader::mergeTlogInto(
        const std::shared_ptr<scope::Context> &context,
        const std::shared_ptr<Topics> &root,
        const std::filesystem::path &path,
        bool forceTimestamp,
        const std::function<bool(ConfigNode &)> &mergeCondition,
        ConfigurationMode configurationMode) {
        std::ifstream stream;
        stream.exceptions(std::ios_base::failbit | std::ios_base::badbit);
        stream.open(path);
        stream.exceptions(std::ios_base::badbit);
        mergeTlogInto(context, root, stream, forceTimestamp, mergeCondition, configurationMode);
    }
} // namespace config
