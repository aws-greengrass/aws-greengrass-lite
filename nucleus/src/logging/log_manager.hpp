#pragma once
#include "data/handle_table.hpp"
#include "data/safe_handle.hpp"
#include "data/shared_struct.hpp"
#include "errors/error_base.hpp"
#include "util/nucleus_paths.hpp"
#include <fstream>
#include <logging.hpp>
#include <map>
#include <util.hpp>

namespace config {
    class Topics;
}

namespace plugins {
    class AbstractPlugin;
}

namespace logging {

    class LogQueue;
    class LogManager;

    struct NucleusLoggingTraits {
        using SymbolType = data::Symbol;
        using SymbolArgType = const data::Symbolish &;
        using ArgType = data::StructElement;
        using StructType = std::shared_ptr<data::SharedStruct>;
        using StructArgType = const StructType &;
        using ErrorType = errors::Error;

        static data::Symbol intern(std::string_view sv);

        static StructType newStruct();
        static StructType cloneStruct(StructArgType s) {
            return std::static_pointer_cast<data::SharedStruct>(s->copy());
        }
        static void putStruct(StructArgType s, SymbolArgType key, const ArgType &value) {
            s->put(key, value);
        }
        static void setLevel(SymbolArgType) {
            throw std::logic_error("Unexpected call");
        }
        static SymbolType getLevel(uint64_t &, SymbolArgType) {
            throw std::logic_error("Unexpected call");
        }
        static void logEvent(StructArgType) {
            throw std::logic_error("Unexpected call");
        }

        static std::shared_ptr<LogManagerBase<NucleusLoggingTraits>> getManager();
    };

    class LogConfigUpdate {
        LogManager &_manager;
        std::shared_ptr<data::StructModelBase> _configs;
        std::shared_ptr<util::NucleusPaths> _paths;

    public:
        LogConfigUpdate(
            LogManager &manager,
            const std::shared_ptr<data::StructModelBase> &configs,
            const std::shared_ptr<util::NucleusPaths> &paths)
            : _manager(manager), _configs(configs), _paths(paths) {
        }
        [[nodiscard]] std::optional<Level> getLevel() const;
        [[nodiscard]] std::optional<Format> getFormat() const;
        [[nodiscard]] std::optional<OutputType> getOutputType() const;
        [[nodiscard]] std::optional<uint64_t> getFileSizeKB() const;
        [[nodiscard]] std::optional<uint64_t> getTotalLogsSizeKB() const;
        [[nodiscard]] std::optional<std::filesystem::path> getOutputDirectory() const;
        [[nodiscard]] std::optional<data::Symbol> getUpCaseSymbol(const data::Symbol &key) const;
        [[nodiscard]] std::optional<std::string> getString(const data::Symbol &key) const;
        [[nodiscard]] std::optional<uint64_t> getInt(const data::Symbol &key) const;
        [[nodiscard]] std::filesystem::path getDefaultOutputDirectory() const;
    };

    class LogState {
        constexpr static std::string_view DEFAULT_LOG_BASE{"greengrass"};
        constexpr static std::string_view LOG_EXTENSION{".log"};
        constexpr static uint64_t DEFAULT_MAX_FILE_SIZE_KB{1024L};
        constexpr static uint64_t DEFAULT_MAX_FILE_SIZE_ALL_KB{DEFAULT_MAX_FILE_SIZE_KB * 10};
        mutable std::shared_mutex _mutex;
        std::string _contextName;
        Level _level{Level::Info};
        Format _format{Format::Text};
        OutputType _outputType{OutputType::Console}; // until file specified
        uint64_t _fileSizeKB{DEFAULT_MAX_FILE_SIZE_KB};
        uint64_t _totalLogsSizeKB{DEFAULT_MAX_FILE_SIZE_ALL_KB};
        std::string _lastDateHour;
        uint64_t _lastFileCounter{0};
        std::filesystem::path _outputDirectory;
        std::ofstream _stream;
        using FormatEnum = util::Enum<Format, Format::Text, Format::Json>;
        void writeLog(
            const std::shared_ptr<data::StructModelBase> &data,
            const FormatEnum::ConstType<Format::Text> &);
        void writeLog(
            const std::shared_ptr<data::StructModelBase> &data,
            const FormatEnum::ConstType<Format::Json> &);

    public:
        explicit LogState(std::string_view contextName);
        std::string getContextName() const {
            return _contextName;
        }
        std::filesystem::path getLogPath(bool forRotation) const;
        std::ostream &stream();
        bool applyConfig(const LogConfigUpdate &source);
        bool mergeConfig(const LogConfigUpdate &source);
        Level getLevel() const {
            std::shared_lock guard{_mutex};
            return _level;
        }
        Format getFormat() const {
            std::shared_lock guard{_mutex};
            return _format;
        }
        OutputType getOutputType() const {
            std::shared_lock guard{_mutex};
            return _outputType;
        }
        void setLevel(Level newLevel) {
            std::unique_lock guard{_mutex};
            _level = newLevel;
        }
        void writeLog(const std::shared_ptr<data::StructModelBase> &data);
        void changeOutput();
        void syncOutput();
        void rotateLog(std::size_t);
        std::string dateHour();
    };

    class LogManager : public LogManagerBase<NucleusLoggingTraits>, protected scope::UsesContext {
        using Traits = NucleusLoggingTraits;
        mutable std::shared_mutex _mutex;
        std::shared_ptr<LogQueue> _queue;
        std::map<std::string, std::shared_ptr<LogState>> _state;
        std::shared_ptr<LogState> _defaultState{std::make_shared<LogState>("")};
        std::atomic_uint64_t _counter{1};

    protected:
        friend class LogConfigUpdate;
        const SymbolType TEXT_FORMAT{Traits::intern("TEXT")};
        const SymbolType JSON_FORMAT{Traits::intern("JSON")};

        const SymbolType CONSOLE_TYPE{Traits::intern("CONSOLE")};
        const SymbolType FILE_TYPE{Traits::intern("FILE")};

        // Config keys
        const SymbolType CONFIG_LEVEL_KEY{Traits::intern("level")};
        const SymbolType CONFIG_FORMAT_KEY{Traits::intern("format")};
        const SymbolType CONFIG_OUTPUT_TYPE_KEY{Traits::intern("outputType")};
        const SymbolType CONFIG_FILE_SIZE_KEY{Traits::intern("fileSizeKB")};
        const SymbolType CONFIG_TOTAL_LOG_SIZE_KEY{Traits::intern("totalLogsSizeKB")};
        const SymbolType CONFIG_OUTPUT_DIRECTORY_KEY{Traits::intern("outputDirectory")};

        const util::LookupTable<SymbolType, Format, 2> FORMAT_MAP{
            TEXT_FORMAT,
            Format::Text,
            JSON_FORMAT,
            Format::Json,
        };

        const util::LookupTable<SymbolType, OutputType, 2> OUTPUT_TYPE_MAP{
            CONSOLE_TYPE,
            OutputType::Console,
            FILE_TYPE,
            OutputType::File,
        };

    public:
        explicit LogManager(const scope::UsingContext &context);
        ~LogManager() override;
        static std::string getModuleName(const std::shared_ptr<plugins::AbstractPlugin> &module);
        std::shared_ptr<LogState> getState(std::string_view contextName) const;
        std::shared_ptr<LogState> createState(std::string_view contextName);
        std::shared_ptr<LogQueue> publishQueue() const;
        data::Symbol getLevel(
            const std::shared_ptr<plugins::AbstractPlugin> &module,
            uint64_t &counter,
            const data::Symbol &logLevel) const;
        Level getLevel(std::string_view contextName, uint64_t &counter, Level logLevel) const;
        void setLevel(
            const std::shared_ptr<plugins::AbstractPlugin> &module, const data::Symbol &logLevel);
        void setLevel(std::string_view contextName, Level logLevel);
        void logEvent(
            const std::shared_ptr<plugins::AbstractPlugin> &module,
            const std::shared_ptr<data::StructModelBase> &entry);
        void logEvent(
            std::string_view contextName, const std::shared_ptr<data::StructModelBase> &entry);
        void reconfigure(std::string_view contextName, const LogConfigUpdate &config);

        void setLevel(Level level) override;
        Level getLevel(uint64_t &counter, Level priorLevel) const override;
        void logEvent(StructArgType entry) override;
    };

    using Logger = LoggerBase<NucleusLoggingTraits>;
} // namespace logging
