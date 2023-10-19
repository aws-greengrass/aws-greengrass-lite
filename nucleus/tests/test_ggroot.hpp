#pragma once
#include "lifecycle/command_line.hpp"
#include "lifecycle/kernel.hpp"
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
        data::Global global;
        data::SysProperties sysProps;
        std::vector<std::string> args;
        lifecycle::Kernel kernel;
        std::thread kernelThread;
        std::atomic_int result;
        std::atomic_bool finished;

        GGRoot() : kernel(global) {
        }

        void preLaunch() {
            lifecycle::CommandLine commandLine{global, kernel};
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
