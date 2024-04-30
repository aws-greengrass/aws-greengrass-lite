#include "tasks/expire_time.hpp"
#include <catch2/catch_all.hpp>
#include <chrono>

// NOLINTBEGIN

SCENARIO("Expire Time Class", "[Expire]") {

    GIVEN("a future expire time") {
        tasks::ExpireTime e = tasks::ExpireTime::fromNowMillis(100);

        WHEN("Checking the expiration") {
            THEN("Expiration not yet") {
                bool notExpired = e.remaining() != std::chrono::milliseconds::zero();
                REQUIRE(notExpired);
                AND_THEN("Time expired") {
                    sleep(1);
                    REQUIRE(e.remaining() < std::chrono::milliseconds::zero());
                }
            }
        }
    }
}

// NOLINTEND
