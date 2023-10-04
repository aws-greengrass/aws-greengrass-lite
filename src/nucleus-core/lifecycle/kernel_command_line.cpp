#include "kernel_command_line.h"
#include <c_api.h>
#include <optional>
#include <util.h>
namespace fs = std::filesystem;

void lifecycle::KernelCommandLine::parseArgs(int argc, char *argv[]) { // NOLINT(*-avoid-c-arrays)
    std::vector<std::string> args;
    args.reserve(argc - 1);
    if(argv[0]) { // NOLINT(*-pointer-arithmetic)
        parseProgramName(argv[0]); // NOLINT(*-pointer-arithmetic)
    } else {
        throw std::runtime_error("Handle nullptr");
    }
    for(int i = 1; i < argc; i++) {
        args.emplace_back(argv[i]); // NOLINT(*-pointer-arithmetic)
    }
    parseArgs(args);
}

void lifecycle::KernelCommandLine::parseProgramName(std::string_view progName) {
    if(progName.empty()) {
        return;
    }
    fs::path progPath{progName};
    progPath = absolute(progPath);
    if(!exists(progPath)) {
        // assume this is not usable for extracting directory information
        return;
    }
    fs::path root{progPath.parent_path()};
    if(root.filename().generic_string() == "bin") {
        root = root.parent_path(); // strip the /bin
    }
    std::unique_lock guard{_mutex};
    _programRootDir = root;
}

std::string lifecycle::KernelCommandLine::nextArg(
    const std::vector<std::string> &args, std::vector<std::string>::const_iterator &iter
) {
    if(iter == args.end()) {
        throw std::runtime_error("Expecting argument");
    }
    std::string v = *iter;
    ++iter;
    return v;
}

void lifecycle::KernelCommandLine::parseHome(data::SysProperties &env) {
    std::unique_lock guard{_mutex};
    std::optional<std::string> homePath = env.get("HOME");
    if(homePath.has_value() && !homePath.value().empty()) {
        _userHomeDir = fs::absolute(fs::path(homePath.value()));
        return;
    }
    homePath = env.get("USERPROFILE");
    if(homePath.has_value() && !homePath.value().empty()) {
        _userHomeDir = fs::absolute(fs::path(homePath.value()));
        return;
    }
    homePath = env.get("HOMEPATH");
    std::optional<std::string> homeDrive = env.get("HOMEDRIVE");
    if(homePath.has_value() && homeDrive.has_value()) {
        fs::path drive{homeDrive.value()};
        fs::path rel{homePath.value()};
        _userHomeDir = fs::absolute(drive / rel);
    } else if(homePath.has_value()) {
        _userHomeDir = fs::absolute(fs::path(homePath.value()));
    } else if(homeDrive.has_value()) {
        _userHomeDir = fs::absolute(fs::path(homeDrive.value()));
    } else {
        _userHomeDir = fs::absolute(".");
    }
}

void lifecycle::KernelCommandLine::parseEnv(data::SysProperties &env) {
    parseHome(env);
}

void lifecycle::KernelCommandLine::parseArgs(const std::vector<std::string> &args) {
    std::unique_lock guard{_mutex};
    for(auto i = args.begin(); i != args.end();) {
        std::string op = nextArg(args, i);
        // TODO: GG-Java ignores case
        if(op == "--config" || op == "-i") {
            std::string arg = nextArg(args, i);
            _providedConfigPathName = deTilde(arg);
            continue;
        }
        if(op == "--init-config" || op == "-init") {
            std::string arg = nextArg(args, i);
            _providedInitialConfigPath = deTilde(arg);
            continue;
        }
        if(op == "--root" || op == "-r") {
            std::string arg = nextArg(args, i);
            _programRootDir = deTilde(arg);
            continue;
        }
        if(op == "--aws-region" || op == "-ar") {
            std::string arg = nextArg(args, i);
            _awsRegionFromCmdLine = arg;
            continue;
        }
        if(op == "--env-stage" || op == "-es") {
            std::string arg = nextArg(args, i);
            _envStageFromCmdLine = arg;
            continue;
        }
        if(op == "--component-default-user" || op == "-u") {
            std::string arg = nextArg(args, i);
            _defaultUserFromCmdLine = arg;
            continue;
        }
        throw std::runtime_error(std::string("Unrecognized command: ") + op);
    }
    //    if (rootAbsolutePath.empty() && !_providedInitialConfigPath.empty() &&
    //                exists(std::filesystem::path(_providedInitialConfigPath)))
    //                {
    //        throw std::runtime_error("TODO: read path from config");
    //    }
    if(_programRootDir.empty()) {
        // deviates from GG-Java, which uses "~/.greengrass"
        throw std::runtime_error("No root path");
    }
    //
    // TODO: continue this code
    //
}

fs::path lifecycle::KernelCommandLine::resolve(const fs::path &first, const fs::path &second) {
    return fs::absolute(first / second);
}

fs::path lifecycle::KernelCommandLine::resolve(const fs::path &first, std::string_view second) {
    return fs::absolute(first / second);
}

fs::path lifecycle::KernelCommandLine::deTilde(std::string_view s) const {
    // converts a string-form path into an OS path
    // replicates GG-Java
    // TODO: code tracks GG-Java, noting that '/' and '\\' should be
    // interchangable for Windows
    if(util::startsWith(s, HOME_DIR_PREFIX)) {
        return resolve(_userHomeDir, util::trimStart(s, HOME_DIR_PREFIX));
    }
    // TODO: resolve with nucleus paths
    return s;
}

int lifecycle::KernelCommandLine::main() {
    data::Global &global = data::Global::self();
    auto threadTask = ggapiClaimThread(); // assume long-running thread, this
                                          // provides a long-running task handle

    // This needs to be subsumed by the lifecyle management - not yet
    // implemented, so current approach is hacky
    global.loader->discoverPlugins();
    std::shared_ptr<data::StructModelBase> emptyStruct{
        std::make_shared<data::SharedStruct>(global.environment)}; // TODO, empty for now
    global.loader->lifecycleBootstrap(emptyStruct);
    global.loader->lifecycleDiscover(emptyStruct);
    global.loader->lifecycleStart(emptyStruct);
    global.loader->lifecycleRun(emptyStruct);

    (void) ggapiWaitForTaskCompleted(
        threadTask,
        -1
    ); // essentially blocks forever but
       // allows main thread to do work
    global.loader->lifecycleTerminate(emptyStruct);
    return 0; // currently unreachable
}
