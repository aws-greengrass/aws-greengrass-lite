#include <catch2/catch_all.hpp>
#include "data/globals.hpp"
#include "test_ggroot.hpp"

namespace fs = std::filesystem;

// NOLINTBEGIN

SCENARIO("Basic Kernel lifecycle", "[kernel]") {
    // Incomplete tests, just getting started
    test::GGRoot ggRoot;
    GIVEN("A very basic startup") {
        ggRoot.args.push_back("--root");
        ggRoot.args.push_back(ggRoot.getDir().generic_string());
        ggRoot.preLaunch();
        ggRoot.launchAsync();
        ggRoot.kernel.softShutdown(std::chrono::seconds(1));
        ggRoot.join();
        THEN("Config files were created") {
            REQUIRE(fs::exists(ggRoot.getDir() / "config"));
            REQUIRE(fs::exists(ggRoot.getDir() / "config" / "config.tlog"));
            REQUIRE(fs::exists(ggRoot.getDir() / "config" / "effectiveConfig.yaml"));
        }
    }
}

// NOLINTEND
