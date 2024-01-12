#include "command_line.hpp"
#include "kernel.hpp"
#include "scope/context_full.hpp"
#include <algorithm>
#include <optional>
#include <util.hpp>

#include <stdexcept>
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

    void CommandLine::parseRawProgramNameAndArgs(util::Span<char *> args) {
        if(args.empty()) {
            throw std::invalid_argument("No program name given");
        }
        if(std::find(args.begin(), args.end(), nullptr) != args.end()) {
            throw std::invalid_argument("Null pointer in arguments");
        }
        parseProgramName(args.front());
        parseArgs({std::next(args.begin()), args.end()});
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

    void CommandLine::parseHome(lifecycle::SysProperties &env) {
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

    void CommandLine::parseEnv(lifecycle::SysProperties &env) {
        parseHome(env);
    }

    void CommandLine::parseArgs(const std::vector<std::string> &args) {

        for(auto i = args.begin(); i != args.end(); i++) {
            bool handled = false;
            for(const auto &j : argumentList) {
                if(j->process(i)) {
                    handled = true;
                    break;
                }
            }
            if(!handled) {
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
        if(_kernel.getPaths()->rootPath().empty()) {
            LOG.atError().event("system-boot-error").logAndThrow(errors::BootError{"No root path"});
        }
    }

} // namespace lifecycle
