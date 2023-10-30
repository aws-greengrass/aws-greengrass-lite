#pragma once

#include "config_manager.hpp"
#include "json_helper.hpp"
#include "util/commitable_file.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>

namespace config {
    class TlogReader;
    class TlogWriter;

    enum class ConfigurationMode : uint32_t { SKELETON_ONLY, WITH_VALUES };

    //
    // Parse transaction logs into configuration
    //
    class TlogReader {

        static constexpr uint32_t VALIDATION_BUFFER_SIZE{256};

    public:
        static bool handleTlogTornWrite(
            data::Environment &environment, const std::filesystem::path &tlogFile
        );

        static void mergeTlogInto(
            data::Environment &environment,
            const std::shared_ptr<Topics> &root,
            std::ifstream &stream,
            bool forceTimestamp,
            const std::function<bool(ConfigNode &)> &mergeCondition = [](auto &) { return true; },
            ConfigurationMode configurationMode = ConfigurationMode::WITH_VALUES
        );

        static void mergeTlogInto(
            data::Environment &environment,
            const std::shared_ptr<Topics> &root,
            const std::filesystem::path &path,
            bool forceTimestamp,
            const std::function<bool(ConfigNode &)> &mergeCondition = [](auto &) { return true; },
            ConfigurationMode configurationMode = ConfigurationMode::WITH_VALUES
        );
    };

    //
    // Watch hook for transaction logs
    //
    class TlogWatcher : public Watcher {
        TlogWriter &_writer;

    public:
        explicit TlogWatcher(TlogWriter &writer) : _writer(writer) {
        }

        void changed(
            const std::shared_ptr<Topics> &topics, data::StringOrd key, WhatHappened changeType
        ) override;

        void childChanged(
            const std::shared_ptr<Topics> &topics, data::StringOrd key, WhatHappened changeType
        ) override;

        void initialized(
            const std::shared_ptr<Topics> &topics, data::StringOrd key, WhatHappened changeType
        ) override;
    };

    //
    // Transaction log writer / maintainer
    //
    class TlogWriter {
        static constexpr auto TRUNCATE_TLOG_EVENT{"truncate-tlog"};
        static constexpr long DEFAULT_MAX_TLOG_ENTRIES{15'000};

        data::Environment &_environment;
        mutable std::mutex _mutex;
        util::CommitableFile _tlogFile{};
        std::shared_ptr<Topics> _root;
        std::shared_ptr<TlogWatcher> _watcher;
        bool _truncateQueue{false};
        uint32_t _count{0}; // entries written so far
        bool _flushImmediately{false};
        bool _autoTruncate{false};
        uint32_t _maxEntries{DEFAULT_MAX_TLOG_ENTRIES};
        uint32_t _retryCount{0};

        static void writeAll(const std::shared_ptr<Topics> &node);

    public:
        TlogWriter(
            data::Environment &environment,
            const std::shared_ptr<Topics> &root,
            const std::filesystem::path &outputPath
        );
        TlogWriter(const TlogWriter &) = delete;
        TlogWriter(TlogWriter &&) = delete;
        TlogWriter &operator=(const TlogWriter &) = delete;
        TlogWriter &operator=(TlogWriter &&other) = delete;
        ~TlogWriter() = default;

        static void abandon();
        static void commit();
        TlogWriter &withAutoTruncate(bool f = true);
        TlogWriter &withWatcher(bool f = true);
        TlogWriter &withMaxEntries(uint32_t maxEntries = DEFAULT_MAX_TLOG_ENTRIES);
        TlogWriter &writeAll();
        TlogWriter &flushImmediately(bool f = true);
        TlogWriter &startNew();
        TlogWriter &append();
        static std::filesystem::path getPath() ;
        void childChanged(ConfigNode &node, WhatHappened changeType);
        TlogWriter &dump();

        static std::filesystem::path getOldTlogPath(const std::filesystem::path &path);
    };
} // namespace config
