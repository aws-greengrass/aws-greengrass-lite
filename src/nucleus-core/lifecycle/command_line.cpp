#include "command_line.h"
#include "kernel.h"
#include <c_api.h>
#include <util.h>
#include <optional>
namespace fs = std::filesystem;

namespace lifecycle {
    //
    // Note that in GG-Java, the command line is first parsed by
    // GreengrassSetup, and some commands are then passed to Kernel, which then delegates further commands
    // to KernelCommandLine - all of which is unnecessary complexity.
    //
    // There are three somewhat intertwined objects - Kernel, CommandLine and Kernel
    //
    // In GG-Lite, all commandline parsing is delegated to this class
    // and values injected directly into Kernel
    // which essentially subsumes parts of GreengrassSetup, Kernel::parseArgs and KernelCommandLine::parseArgs
    //

    void CommandLine::parseArgs(int argc, char * argv[]) { // NOLINT(*-avoid-c-arrays)
        std::vector<std::string> args;
        args.reserve(argc-1);
        if (argv[0]) { // NOLINT(*-pointer-arithmetic)
            parseProgramName(argv[0]); // NOLINT(*-pointer-arithmetic)
        } else {
            throw std::runtime_error("Handle nullptr");
        }
        for (int i = 1; i < argc; i++) {
            args.emplace_back(argv[i]); // NOLINT(*-pointer-arithmetic)
        }
        parseArgs(args);
    }

    void CommandLine::parseProgramName(std::string_view progName) {
        if (progName.empty()) {
            return;
        }
        fs::path progPath {progName};
        progPath = absolute(progPath);
        if (!exists(progPath)) {
            // assume this is not usable for extracting directory information
            return;
        }
        fs::path root { progPath.parent_path() };
        if (root.filename().generic_string() == util::NucleusPaths::BIN_PATH_NAME) {
            root = root.parent_path(); // strip the /bin
        }
        _kernel.getPaths()->setRootPath(root, true /* passive */);
    }

    std::string CommandLine::nextArg(const std::vector<std::string> & args, std::vector<std::string>::const_iterator & iter) {
        if (iter == args.end()) {
            throw std::runtime_error("Expecting argument");
        }
        std::string v = *iter;
        ++iter;
        return v;
    }

    void CommandLine::parseHome(data::SysProperties &env) {
        std::optional<std::string> homePath = env.get("HOME");
        std::shared_ptr<util::NucleusPaths> paths = _kernel.getPaths();
        if (homePath.has_value() && !homePath.value().empty()) {
            paths->setHomePath(fs::absolute(fs::path(homePath.value())));
            return;
        }
        homePath = env.get("USERPROFILE");
        if (homePath.has_value() && !homePath.value().empty()) {
            paths->setHomePath(fs::absolute(fs::path(homePath.value())));
            return;
        }
        homePath = env.get("HOMEPATH");
        std::optional<std::string> homeDrive = env.get("HOMEDRIVE");
        if (homePath.has_value() && homeDrive.has_value()) {
            fs::path drive {homeDrive.value()};
            fs::path rel {homePath.value()};
            paths->setHomePath(fs::absolute(drive/rel));
        } else if (homePath.has_value()) {
            paths->setHomePath(fs::absolute(fs::path(homePath.value())));
        } else if (homeDrive.has_value()) {
            paths->setHomePath(fs::absolute(fs::path(homeDrive.value())));
        } else {
            paths->setHomePath(fs::absolute("."));
        }
    }

    void CommandLine::parseEnv(data::SysProperties &env) {
        parseHome(env);
    }

    void CommandLine::parseArgs(const std::vector<std::string> & args) {
        std::shared_ptr<util::NucleusPaths> paths = _kernel.getPaths();
        for (auto i = args.begin(); i != args.end(); ) {
            std::string op = nextArg(args, i);
            // TODO: GG-Java ignores case
            if (op == "--config" || op == "-i") {
                std::string arg = nextArg(args, i);
                _providedConfigPath = paths->deTilde(arg);
                continue;
            }
            if (op == "--init-config" || op == "-init") {
                std::string arg = nextArg(args, i);
                _providedInitialConfigPath = paths->deTilde(arg);
                continue;
            }
            if (op == "--root" || op == "-r") {
                std::string arg = nextArg(args, i);
                _kernel.getPaths()->setRootPath(paths->deTilde(arg), false);
                continue;
            }
            if (op == "--aws-region" || op == "-ar") {
                std::string arg = nextArg(args, i);
                _awsRegionFromCmdLine = arg;
                continue;
            }
            if (op == "--env-stage" || op == "-es") {
                std::string arg = nextArg(args, i);
                _envStageFromCmdLine = arg;
                continue;
            }
            if (op == "--component-default-user" || op == "-u") {
                std::string arg = nextArg(args, i);
                _defaultUserFromCmdLine = arg;
                continue;
            }
            throw std::runtime_error(std::string("Unrecognized command: ") + op);
        }
        // TODO: Not sure of value of this code for GG-Lite as we should always determine a root
//        if (_programRootDir.empty() && !_providedInitialConfigPath.empty() &&
//                    exists(std::filesystem::path(_providedInitialConfigPath))) {
//            config::Manager tempManager {_global.environment};
//            std::string path = tempManager.read(_providedInitialConfigPath).lookup()["system"].get("rootpath").getString();
//            if (!path.empty()) {
//                _programRootDir = deTilde(path);
//            }
//        }
        if (_kernel.getPaths()->rootPath().empty()) {
            // deviates from GG-Java, which uses "~/.greengrass"
            // assume not applicable for GG-lite as a root path can always be determined
            throw std::runtime_error("No root path");
        }
    }

}
