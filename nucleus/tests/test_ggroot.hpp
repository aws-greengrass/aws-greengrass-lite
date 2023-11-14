#pragma once
#include "lifecycle/command_line.hpp"
#include "lifecycle/kernel.hpp"
#include "scope/context_full.hpp"
#include "test_tools.hpp"
#include <atomic>
#include <thread>

namespace test {

    class GGRoot : public TempDir {
    protected:
        void threadRunner() {
            int res = kernel.launch();
            result.store(res);
            finished.store(true);
        }

    public:
        scope::LocalizedScope scope{scope::Context::create()};
        lifecycle::SysProperties sysProps;
        std::vector<std::string> args;
        lifecycle::Kernel kernel;
        std::thread kernelThread;
        std::atomic_int result{0};
        std::atomic_bool finished{false};

        GGRoot() : kernel(scope.context()->context()) {
        }

        void preLaunch() {
            lifecycle::CommandLine commandLine{scope.context()->context(), kernel};
            commandLine.parseArgs(args);
            kernel.preLaunch(commandLine);
        }

        void launchAsync() {
            kernelThread = std::thread(&GGRoot::threadRunner, this);
        }

        void join() {
            kernelThread.join();
        }
    };
} // namespace test
