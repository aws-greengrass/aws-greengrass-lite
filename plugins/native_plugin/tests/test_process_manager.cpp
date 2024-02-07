#include "test_util.hpp"
#include <abstract_process_manager.hpp>
#include <catch2/catch_all.hpp>

#include <native_plugin.hpp>
#include <startable.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <type_traits>

SCENARIO("Process Manager (Posix)", "[native]") {
    GIVEN("Process Manager instance") {
        ipc::ProcessManager manager;
        GIVEN("Startable") {
            std::mutex m;
            std::atomic_int returnCode{1};
            std::atomic_bool done{false};
            std::condition_variable cv;

            // TODO: how to load a configuration for shell from tests

            std::string output;
            std::string error;
            ipc::Startable startable;
            startable.withCommand("/bin/sh")
                .withArguments({"-c", "sleep 5; echo Hello World!"})
                .withOutput([&output](util::Span<const char> buffer) {
                    // TODO: timestamping
                    output.append(buffer.data(), buffer.size());
                })
                .withError([&error](util::Span<const char> buffer) {
                    error.append(buffer.data(), buffer.size());
                })
                .withCompletion([&cv, &done, &returnCode](int rc) {
                    returnCode.store(rc);
                    done.store(true);
                    cv.notify_one();
                });

            using namespace std::chrono_literals;
            WHEN("Running the process and registering it") {

                const auto expectedStopTime = std::chrono::high_resolution_clock::now() + 5s;

                auto process = startable.start();
                REQUIRE(process != nullptr);
                REQUIRE(process->isRunning());

                THEN("Program runs and succeeds") {
                    auto pid = manager.registerProcess(std::move(process));
                    REQUIRE(pid.id >= 0);

                    std::unique_lock lock{m};
                    REQUIRE(cv.wait_for(lock, 10s, [&done] { return done.load(); }));

                    auto t2 = std::chrono::high_resolution_clock::now();

                    auto code = returnCode.load();
                    REQUIRE(done.load() == true);
                    REQUIRE(code == 0);
                    REQUIRE(error.empty());
                    REQUIRE(output == "Hello World!\n");

                    auto deviation = expectedStopTime - std::chrono::high_resolution_clock::now();
                    REQUIRE(std::chrono::abs(deviation) <= 1s);
                }
            }
        }
    }
}
