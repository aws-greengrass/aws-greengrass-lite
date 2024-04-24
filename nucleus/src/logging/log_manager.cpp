#include "log_manager.hpp"
#include "data/shared_buffer.hpp"
#include "logging/log_queue.hpp"
#include "scope/context_full.hpp"

namespace logging {

    LogState::LogState(std::string_view contextName) : _contextName(contextName) {
    }

    void LogManager::logEvent(StructArgType entry) {
        // Nucleus override
        logEvent("", entry);
    }

    void LogManager::logEvent(
        const std::shared_ptr<plugins::AbstractPlugin> &module,
        const std::shared_ptr<data::StructModelBase> &entry) {
        logEvent(getModuleName(module), entry);
    }

    void LogManager::logEvent(
        std::string_view contextName, const std::shared_ptr<data::StructModelBase> &entry) {
        if(!contextName.empty()) {
            entry->put(MODULE_KEY, contextName);
        }
        auto state = getState(contextName);
        _queue->publish(state, entry);
    }

    void LogManager::setLevel(
        const std::shared_ptr<plugins::AbstractPlugin> &module, const data::Symbol &logLevel) {
        setLevel(getModuleName(module), toLevel(logLevel));
    }

    void LogManager::setLevel(Level logLevel) {
        setLevel("", logLevel);
    }

    void LogManager::setLevel(std::string_view contextName, Level logLevel) {
        auto state = createState(contextName);
        state->setLevel(logLevel);
        _counter++; // invalidate caches
    }

    std::string LogManager::getModuleName(const std::shared_ptr<plugins::AbstractPlugin> &module) {
        if(module) {
            return module->getName();
        } else {
            return "";
        }
    }

    data::Symbol LogManager::getLevel(
        const std::shared_ptr<plugins::AbstractPlugin> &module,
        uint64_t &counter,
        const data::Symbol &logLevel) const {

        Level prior = toLevel(logLevel);
        Level newLevel = getLevel(getModuleName(module), counter, toLevel(logLevel));
        if(newLevel == prior) {
            return logLevel;
        } else {
            return toSymbol(newLevel);
        }
    }

    Level LogManager::getLevel(uint64_t &counter, Level logLevel) const {
        return getLevel("", counter, logLevel);
    }

    Level LogManager::getLevel(
        std::string_view contextName, uint64_t &counter, Level logLevel) const {

        if(counter == _counter) {
            return logLevel; // use cached value
        }
        auto state = getState(contextName);
        counter = _counter;
        return state->getLevel();
    }

    LogManager::LogManager(const scope::UsingContext &context)
        : scope::UsesContext(context), _queue(std::make_shared<LogQueue>(context)) {
    }

    LogManager::~LogManager() {
        _queue->stop();
        _queue.reset();
    }

    std::shared_ptr<LogState> LogManager::getState(std::string_view contextName) const {
        std::shared_lock guard{_mutex};
        if(!contextName.empty()) {
            std::string contextNameAsString{contextName};
            auto state = _state.find(contextNameAsString);
            if(state != _state.end()) {
                return state->second;
            }
        }
        return _defaultState;
    }

    std::shared_ptr<LogState> LogManager::createState(std::string_view contextName) {
        std::unique_lock guard{_mutex};
        if(contextName.empty()) {
            return _defaultState;
        }
        std::string contextNameAsString{contextName};
        auto state = _state.find(contextNameAsString);
        if(state != _state.end()) {
            return state->second;
        }
        auto newState = std::make_shared<LogState>(contextName);
        _state.emplace(contextNameAsString, newState);
        return newState;
    }

    std::shared_ptr<LogQueue> LogManager::publishQueue() const {
        return _queue;
    }
    void LogManager::reconfigure(std::string_view contextName, const LogConfigUpdate &config) {
        auto state = createState(contextName);
        bool changed = state->applyConfig(config);
        if(changed) {
            _queue->reconfigure(state); // synchronize through queue
            _counter++; // invalidate caches
        }
    }

    data::Symbol NucleusLoggingTraits::intern(std::string_view sv) {
        return scope::context()->intern(sv);
    }

    std::shared_ptr<LogManagerBase<NucleusLoggingTraits>> NucleusLoggingTraits::getManager() {
        return scope::context()->logManager().baseRef();
    }

    NucleusLoggingTraits::StructType NucleusLoggingTraits::newStruct() {
        return std::make_shared<data::SharedStruct>(scope::context());
    }

    std::optional<std::string> LogConfigUpdate::getString(const data::Symbol &key) const {
        if(!_configs) {
            return {};
        }
        auto genValue = _configs->get(key);
        if(!genValue.isScalar()) {
            return {};
        }
        return genValue.getString();
    }

    std::optional<uint64_t> LogConfigUpdate::getInt(const data::Symbol &key) const {
        if(!_configs) {
            return {};
        }
        auto genValue = _configs->get(key);
        if(!genValue.isScalar()) {
            return {};
        }
        return genValue.getInt();
    }

    std::optional<data::Symbol> LogConfigUpdate::getUpCaseSymbol(const data::Symbol &key) const {
        auto strValue = getString(key);
        if(strValue.has_value()) {
            data::Symbol symValue = key.table().testAndGetSymbol(util::upper(strValue.value()));
            if(symValue) {
                return symValue;
            }
        }
        return {};
    }

    std::optional<Level> LogConfigUpdate::getLevel() const {
        auto levelSymbol = getUpCaseSymbol(_manager.CONFIG_LEVEL_KEY);
        if(levelSymbol.has_value()) {
            return _manager.LEVEL_MAP.lookup(levelSymbol.value());
        } else {
            return {};
        }
    }

    std::optional<Format> LogConfigUpdate::getFormat() const {
        auto formatSymbol = getUpCaseSymbol(_manager.CONFIG_FORMAT_KEY);
        if(formatSymbol.has_value()) {
            return _manager.FORMAT_MAP.lookup(formatSymbol.value());
        } else {
            return {};
        }
    }

    std::optional<OutputType> LogConfigUpdate::getOutputType() const {
        auto outSymbol = getUpCaseSymbol(_manager.CONFIG_OUTPUT_TYPE_KEY);
        if(outSymbol.has_value()) {
            return _manager.OUTPUT_TYPE_MAP.lookup(outSymbol.value());
        } else {
            return {};
        }
    }

    std::optional<uint64_t> LogConfigUpdate::getFileSizeKB() const {
        return getInt(_manager.CONFIG_FILE_SIZE_KEY);
    }

    std::optional<uint64_t> LogConfigUpdate::getTotalLogsSizeKB() const {
        return getInt(_manager.CONFIG_TOTAL_LOG_SIZE_KEY);
    }

    std::optional<std::filesystem::path> LogConfigUpdate::getOutputDirectory() const {
        auto optStrDir = getString(_manager.CONFIG_OUTPUT_DIRECTORY_KEY);
        if(optStrDir.has_value()) {
            std::filesystem::path path = _paths->deTilde(optStrDir.value());
            _paths->createLoggerPath(path); // creates, applies permissions
            return path;
        }
        return {};
    }

    std::filesystem::path LogConfigUpdate::getDefaultOutputDirectory() const {
        auto path = _paths->getDefaultLoggerPath();
        _paths->createLoggerPath(path);
        return path;
    }

    bool LogState::applyConfig(const LogConfigUpdate &source) {
        std::unique_lock guard{_mutex};
        _level = source.getLevel().value_or(DEFAULT_LOG_LEVEL);
        _fileSizeKB = source.getFileSizeKB().value_or(DEFAULT_MAX_FILE_SIZE_KB);
        _totalLogsSizeKB = source.getTotalLogsSizeKB().value_or(DEFAULT_MAX_FILE_SIZE_ALL_KB);
        auto format = source.getFormat().value_or(Format::Text);
        auto outType = source.getOutputType();
        auto outDir = source.getOutputDirectory();
        OutputType outputType;
        std::filesystem::path outputDirectory;

        if(outDir.has_value() && outType.has_value()) {
            outputType = outType.value();
            outputDirectory = outDir.value();
        } else if(outType.has_value()) {
            outputType = outType.value();
            if(outType.value() == OutputType::File) {
                outputDirectory = source.getDefaultOutputDirectory();
            } else {
                outputDirectory = std::filesystem::path{};
            }
        } else {
            outputType = OutputType::File;
            if(outDir.has_value()) {
                outputDirectory = outDir.value();
            } else {
                outputDirectory = source.getDefaultOutputDirectory();
            }
        }
        bool changed =
            format != _format || outputType != _outputType || outputDirectory != _outputDirectory;
        _format = format;
        _outputType = outputType;
        _outputDirectory = outputDirectory;
        return changed;
    }

    bool LogState::mergeConfig(const LogConfigUpdate &source) {
        std::unique_lock guard{_mutex};
        _level = source.getLevel().value_or(_level);
        _fileSizeKB = source.getFileSizeKB().value_or(_fileSizeKB);
        _totalLogsSizeKB = source.getTotalLogsSizeKB().value_or(_totalLogsSizeKB);
        auto format = source.getFormat().value_or(_format);
        auto outputType = source.getOutputType().value_or(_outputType);
        auto outputDirectory = source.getOutputDirectory().value_or(_outputDirectory);
        bool changed =
            format != _format || outputType != _outputType || outputDirectory != _outputDirectory;
        _format = format;
        _outputType = outputType;
        _outputDirectory = outputDirectory;
        return changed;
    }

    std::filesystem::path LogState::getLogPath(bool forRotation) const {
        std::ignore = forRotation; // TODO: This right now completely ignores log rotation
        std::string baseName;
        if(_outputType != OutputType::File || _outputDirectory.empty()) {
            return {};
        }
        if(_contextName.empty()) {
            baseName = DEFAULT_LOG_BASE;
        } else {
            baseName = _contextName;
        }
        baseName += LOG_EXTENSION;
        return _outputDirectory / baseName;
    }

    std::ostream &LogState::stream() {
        if(_stream.is_open()) {
            return _stream;
        } else {
            return std::cerr;
        }
    }

    void LogState::changeOutput() {
        if(_stream.is_open()) {
            _stream.close();
        }
        auto fullPath = getLogPath(false);
        if(!fullPath.empty()) {
            _stream.exceptions(std::ios::failbit | std::ios::badbit);
            _stream.open(fullPath, std::ios_base::app | std::ios_base::out);
        }
    }

    void LogState::syncOutput() {
        if(_stream.is_open()) {
            _stream.flush();
        }
    }

    void LogState::writeLog(const std::shared_ptr<data::StructModelBase> &data) {
        FormatEnum::visitNoRet(_format, [this, &data](const auto &type) { writeLog(data, type); });
    }

    void LogState::writeLog(
        const std::shared_ptr<data::StructModelBase> &data,
        const FormatEnum::ConstType<Format::Json> &) {

        auto buffer = data->toJson();
        stream() << buffer << "\n"; // intentionally not endl - data not flushed immediately
    }

    void LogState::writeLog(
        const std::shared_ptr<data::StructModelBase> &data,
        const FormatEnum::ConstType<Format::Text> &) {

        // TODO: see
        // https://github.com/aws-greengrass/aws-greengrass-logging-java/blob/main/src/main/java/com/aws/greengrass/logging/impl/GreengrassLogMessage.java

        // For now, just dump the JSON
        auto buffer = data->toJson();
        stream() << buffer << "\n";
    }

} // namespace logging
