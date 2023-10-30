#include "command_line.hpp"
#include "kernel.hpp"
#include <optional>
#include <util.hpp>
namespace fs = std::filesystem;

namespace lifecycle {
    //
    // GG-Interop:
    // Note that in GG-Java, the command line is first parsed by GreengrassSetup, and some commands
    // are then passed to Kernel, which then delegates further commands to KernelCommandLine - all
    // of which is combined into this single helper class to improve maintainability.
    //

    // NOLINTBEGIN(*-avoid-c-arrays)
    // NOLINTBEGIN(*-pointer-arithmetic)
    void CommandLine::parseArgs(int argc, char *argv[]) {
        std::vector<std::string> args;
        args.reserve(argc - 1);
        if(argv[0]) {
            parseProgramName(argv[0]);
        } else {
            throw std::runtime_error("Handle nullptr");
        }
        for(int i = 1; i < argc; i++) {
            args.emplace_back(argv[i]);
        }
        parseArgs(args);
        // NOLINTEND(*-pointer-arithmetic)
        // NOLINTEND(*-avoid-c-arrays)
    }

    void CommandLine::parseProgramName(std::string_view progName) {
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
        if(root.filename().generic_string() == util::NucleusPaths::BIN_PATH_NAME) {
            root = root.parent_path(); // strip the /bin
        }
        _kernel.getPaths()->setRootPath(root, true /* passive */);
    }

    static std::string CommandLine::nextArg(
        const std::vector<std::string> &args, std::vector<std::string>::const_iterator &iter
    ) {
        if(iter == args.end()) {
            throw std::runtime_error("Expecting argument");
        }
        std::string v = *iter;
        ++iter;
        return v;
    }

    static void CommandLine::parseHome(data::SysProperties &env) {
        std::optional<std::string> homePath = env.get("HOME");
        std::shared_ptr<util::NucleusPaths> paths = _kernel.getPaths();
        if(homePath.has_value() && !homePath.value().empty()) {
            paths->setHomePath(fs::absolute(fs::path(homePath.value())));
            return;
        }
        homePath = env.get("USERPROFILE");
        if(homePath.has_value() && !homePath.value().empty()) {
            paths->setHomePath(fs::absolute(fs::path(homePath.value())));
            return;
        }
        homePath = env.get("HOMEPATH");
        std::optional<std::string> homeDrive = env.get("HOMEDRIVE");
        if(homePath.has_value() && homeDrive.has_value()) {
            fs::path drive{homeDrive.value()};
            fs::path rel{homePath.value()};
            paths->setHomePath(fs::absolute(drive / rel));
        } else if(homePath.has_value()) {
            paths->setHomePath(fs::absolute(fs::path(homePath.value())));
        } else if(homeDrive.has_value()) {
            paths->setHomePath(fs::absolute(fs::path(homeDrive.value())));
        } else {
            paths->setHomePath(fs::absolute("."));
        }
    }

    void CommandLine::parseEnv(data::SysProperties &env) {
        parseHome(env);
    }

    void CommandLine::parseArgs(const std::vector<std::string> &args) {
        std::shared_ptr<util::NucleusPaths> paths = _kernel.getPaths();
        for(auto i = args.begin(); i != args.end();) {
            std::string op = nextArg(args, i);
            // TODO: GG-Java ignores case
            if(op == "--config" || op == "-i") {
                std::string arg = nextArg(args, i);
                _providedConfigPath = paths->deTilde(arg);
                continue;
            }
            if(op == "--init-config" || op == "-init") {
                std::string arg = nextArg(args, i);
                _providedInitialConfigPath = paths->deTilde(arg);
                continue;
            }
            if(op == "--root" || op == "-r") {
                std::string arg = nextArg(args, i);
                _kernel.getPaths()->setRootPath(paths->deTilde(arg), false);
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
        // GG-Interop:
        // GG-Java will pull root out of initial config if it exists and root is not defined
        // otherwise it will assume "~/.greengrass"
        // however in GG-Lite, root should always be defined by this line.
        if(_kernel.getPaths()->rootPath().empty()) {
            throw std::runtime_error("No root path");
        }
    }

} // namespace lifecycle
