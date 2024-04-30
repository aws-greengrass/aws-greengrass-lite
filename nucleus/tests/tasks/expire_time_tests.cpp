#include "tasks/expire_time.hpp"
#include <catch2/catch_all.hpp>
#include <chrono>
#include <thread>

// NOLINTBEGIN

using namespace std::chrono_literals;

SCENARIO("Expire Time Class", "[Expire]") {

    GIVEN("a future expire time") {
        tasks::ExpireTime e = tasks::ExpireTime::fromNowMillis(100);

        WHEN("Checking the expiration") {
            THEN("Expiration not yet") {
                bool notExpired = e.remaining() > std::chrono::milliseconds::zero();
                REQUIRE(notExpired);
                AND_THEN("Time expired") {
                    std::this_thread::sleep_for(200ms);
                    REQUIRE(e.remaining() < std::chrono::milliseconds::zero());
                }
            }
        }
    }
}

// NOLINTEND
