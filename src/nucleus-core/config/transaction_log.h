#pragma once

#include "config_manager.h"
#include <atomic>
#include <filesystem>
#include <fstream>

namespace config {
    class TlogReader;

    class TlogWriter;

    //
    // Parse transaction logs into configuration
    //
    class TlogReader {

    public:
        static bool validateTlog(const std::filesystem::path &tlogFile) {
            // TODO: missing code
            return false;
        }
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
        std::filesystem::path _tlogOutputPath;
        std::shared_ptr<Topics> _root;
        std::shared_ptr<TlogWatcher> _watcher;
        bool _truncateQueue{false};
        uint32_t _count{0}; // entries written so far
        bool _flushImmediately{false};
        bool _autoTruncate{false};
        uint32_t _maxEntries{DEFAULT_MAX_TLOG_ENTRIES};
        uint32_t _retryCount{0};
        std::ofstream _writer;

        void writeAll(const std::shared_ptr<Topics> &node);

    public:
        TlogWriter(
            data::Environment &environment,
            const std::shared_ptr<Topics> &root,
            const std::filesystem::path &outputPath
        );

        ~TlogWriter();

        void close();

        TlogWriter &withAutoTruncate(bool f = true);

        TlogWriter &withWatcher(bool f = true);

        TlogWriter &withMaxEntries(uint32_t maxEntries = DEFAULT_MAX_TLOG_ENTRIES);

        TlogWriter &writeAll();

        TlogWriter &flushImmediately();

        TlogWriter &open(std::ios_base::openmode mode);

        TlogWriter &open(const std::filesystem::path &path, std::ios_base::openmode mode);

        std::filesystem::path getPath();

        static void dump(
            data::Environment &environment,
            const std::shared_ptr<Topics> &root,
            const std::filesystem::path &outputPath
        );

        void childChanged(
            const std::shared_ptr<Topics> &topics, data::StringOrd key, WhatHappened changeType
        );

        static std::string stringifyWhatHappened(WhatHappened changeType);
    };
} // namespace config
