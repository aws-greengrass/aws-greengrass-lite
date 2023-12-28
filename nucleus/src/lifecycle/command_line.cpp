#include "command_line.hpp"
#include "kernel.hpp"
#include "scope/context_full.hpp"
#include <optional>
#include <util.hpp>

namespace fs = std::filesystem;

const auto LOG = // NOLINT(cert-err58-cpp)
        logging::Logger::of("com.aws.greengrass.lifecycle.CommandLine");

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
        if (argv[0]) {
            parseProgramName(argv[0]);
        } else {
            throw std::runtime_error("Handle nullptr");
        }
        for (int i = 1; i < argc; i++) {
            args.emplace_back(argv[i]);
        }
        parseArgs(args);
        // NOLINTEND(*-pointer-arithmetic)
        // NOLINTEND(*-avoid-c-arrays)
    }

    void CommandLine::parseProgramName(std::string_view progName) {
        if (progName.empty()) {
            return;
        }
        fs::path progPath{progName};
        progPath = absolute(progPath);
        if (!exists(progPath)) {
            // assume this is not usable for extracting directory information
            return;
        }
        fs::path root{progPath.parent_path()};
        if (root.filename().generic_string() == util::NucleusPaths::BIN_PATH_NAME) {
            root = root.parent_path(); // strip the /bin
        }
        _kernel.getPaths()->setRootPath(root, true /* passive */);
    }

    void CommandLine::parseHome(lifecycle::SysProperties &env) {
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
            fs::path drive{homeDrive.value()};
            fs::path rel{homePath.value()};
            paths->setHomePath(fs::absolute(drive / rel));
        } else if (homePath.has_value()) {
            paths->setHomePath(fs::absolute(fs::path(homePath.value())));
        } else if (homeDrive.has_value()) {
            paths->setHomePath(fs::absolute(fs::path(homeDrive.value())));
        } else {
            paths->setHomePath(fs::absolute("."));
        }
    }

    void CommandLine::parseEnv(lifecycle::SysProperties &env) {
        parseHome(env);
    }

    void CommandLine::parseArgs(const std::vector<std::string> &args) {

        for (auto i = args.begin(); i != args.end(); i++) {
            bool handled = false;
            for (const auto &j: argumentList) {
                if (j->process(i)) {
                    handled = true;
                    break;
                }
            }
            if (!handled) {
                LOG.atError()
                        .event("parse-args-error")
                        .logAndThrow(errors::CommandLineArgumentError{
                                std::string("Unrecognized command: ") + *i});
            }
        }

        // GG-Interop:
        // GG-Java will pull root out of initial config if it exists and root is not defined
        // otherwise it will assume "~/.greengrass"
        // however in GG-Lite, root should always be defined by this line.
        if (_kernel.getPaths()->rootPath().empty()) {
            LOG.atError().event("system-boot-error").logAndThrow(errors::BootError{"No root path"});
        }
    }

    CommandLine::CommandLine(
            const std::shared_ptr<scope::Context> &context, lifecycle::Kernel &kernel)
            : _context(context), _kernel(kernel) {

        argumentValue<std::string>::createInstance(argumentList,
                                                   "i",
                                                   "config",
                                                   "configuration Path",
                                                   [this](std::string arg) {
                                                       _providedConfigPath = _kernel.getPaths()->deTilde(arg);
                                                   });

        argumentValue<std::string>::createInstance(argumentList,
                                                   "init",
                                                   "init-config",
                                                   "initial configuration path",
                                                   [this](std::string arg) {
                                                       _providedInitialConfigPath = _kernel.getPaths()->deTilde(arg);
                                                   });


        argumentValue<std::string>::createInstance(argumentList,
                                                   "r",
                                                   "root",
                                                   "the root path selection",
                                                   [this](std::string arg) {
                                                       auto paths = _kernel.getPaths();
                                                       paths->setRootPath(paths->deTilde(arg));
                                                   });

        argumentValue<std::string>::createInstance(argumentList,
                                                   "ar",
                                                   "aws-region",
                                                   "AWS Region",
                                                   [this](std::string arg) { _awsRegionFromCmdLine = arg; });

        argumentValue<std::string>::createInstance(argumentList,
                                                   "es",
                                                   "env-stage",
                                                   "Environment Stage Selection",
                                                   [this](std::string arg) { _envStageFromCmdLine = arg; });

        argumentValue<std::string>::createInstance(argumentList,
                                                   "u",
                                                   "component-default-user",
                                                   "Component Default User",
                                                   [this](std::string arg) { _defaultUserFromCmdLine = arg; });
    }

} // namespace lifecycle
